"""Session/package-scoped dispatcher; no sequencing or retry policy lives here."""
import asyncio
import inspect
import math
import threading
import time
from dataclasses import dataclass
from artest_sdk.api import Context, OperationError, Result
from artest_sdk.schema import decode
from .transport import json_load
from . import artest_process_pb2 as w
import json

STATUS = {"ok": w.OK, "error": w.EXTENSION_FAILURE, "cancelled": w.CANCELLED,
          "timedOut": w.TIMED_OUT, "invalidArgument": w.INVALID_ARGUMENT}

def response(result):
    if not isinstance(result, Result): raise TypeError("Operations must return Result")
    value = w.Response(status=STATUS[result.status], diagnostic=result.message[:1000],
                       effect_indeterminate=result.effect_indeterminate)
    if result.data is not None:
        value.payload.schema_id = result.schema_id
        value.payload.json = json.dumps(result.data, allow_nan=False, separators=(",", ":"))
    return value

@dataclass
class Instance:
    declaration: object
    value: object
    configuration: object
    lock: asyncio.Lock
    initialized: bool = False
    stopped: bool = False

class Worker:
    def __init__(self, transport, extension):
        self.transport, self.extension = transport, extension
        self.instances, self.calls, self.pending = {}, {}, {}
        # Bounded terminal-call tombstones handle cancellation crossing a result
        # in flight. A late ACK confirms receipt, never reopens or replays work.
        self.completed = {}
        self.next_handle, self.next_request, self.last_request = 1, 3, 0
        self.done = asyncio.Event()
        self.failure = None

    async def run(self):
        loop = asyncio.get_running_loop()
        def read():
            try:
                while not self.done.is_set():
                    message = self.transport.receive()
                    loop.call_soon_threadsafe(self.accept, message)
            except BaseException as error:
                loop.call_soon_threadsafe(self.fail, error)
        threading.Thread(target=read, name="ARTest control", daemon=True).start()
        await self.done.wait()
        if self.failure: raise self.failure

    def fail(self, error):
        self.failure = error
        self.done.set()

    def accept(self, message):
        try:
            kind = message.WhichOneof("body")
            if kind == "request":
                if message.correlation % 2 or message.correlation <= self.last_request or len(self.calls) >= 16:
                    raise ValueError("Invalid/duplicate Engine request")
                if message.parent and message.parent not in self.pending: raise ValueError("Unknown service parent")
                self.last_request = message.correlation
                cancelled = asyncio.Event()
                self.calls[message.correlation] = cancelled
                asyncio.create_task(self.dispatch(message, cancelled))
            elif kind == "cancel":
                if message.cancel.acknowledged:
                    raise ValueError("Unknown cancellation")
                active = self.calls.get(message.correlation)
                if active is not None and not active.is_set():
                    active.set()
                elif message.correlation in self.completed and not self.completed[message.correlation]:
                    self.completed[message.correlation] = True
                else:
                    raise ValueError("Unknown or duplicate cancellation")
                reply = self.transport.envelope(message.correlation)
                reply.cancel.acknowledged = True
                self.transport.send(reply)
            elif kind == "response":
                future = self.pending.pop(message.correlation, None)
                if future is None: raise ValueError("Unknown service response")
                future.set_result(message.response)
            else: raise ValueError("Unexpected control message")
        except BaseException as error: self.fail(error)

    def log(self, correlation, message, severity=0):
        if correlation not in self.calls: raise RuntimeError("Expired Context")
        envelope = self.transport.envelope(correlation)
        envelope.event.category = self.extension.identity
        envelope.event.message = str(message)[:1000]
        envelope.event.severity = severity
        self.transport.send(envelope)

    async def service(self, context, kind, operation, payload, handle=0):
        if context._correlation not in self.calls: raise RuntimeError("Expired Context")
        correlation = self.next_request
        self.next_request += 2
        envelope = self.transport.envelope(correlation, context._correlation)
        request = envelope.request
        request.operation = {"resolve": w.RESOLVE_SERVICE, "invoke": w.INVOKE_SERVICE, "release": w.RELEASE_SERVICE}[kind]
        request.component, request.operation_id = handle, operation
        request.payload.schema_id = "artest.schema.generic-json.v1"
        request.payload.json = json.dumps(payload, allow_nan=False)
        request.has_deadline = True
        request.remaining_ms = max(1, int((context._deadline - time.monotonic()) * 1000))
        future = asyncio.get_running_loop().create_future()
        self.pending[correlation] = future
        self.transport.send(envelope)
        result = await future
        if result.status != w.OK:
            state = {w.CANCELLED: "cancelled", w.TIMED_OUT: "timedOut"}.get(result.status, "error")
            raise OperationError(Result(status=state, message=result.diagnostic,
                                        effect_indeterminate=result.effect_indeterminate))
        if kind == "resolve": return result.handle
        return Result(json_load(result.payload.json), result.payload.schema_id or "artest.schema.generic-json.v1")

    async def dispatch(self, envelope, cancelled):
        request = envelope.request
        deadline = time.monotonic() + request.remaining_ms / 1000 if request.has_deadline else math.inf
        payload = json_load(request.payload.json) or {}
        context = Context(self, envelope.correlation, cancelled, deadline, payload.get("instrumentId", ""))
        stop = request.operation == w.STOP
        try:
            if request.operation == w.DESCRIBE:
                result = response(Result(self.extension.describe()))
            elif request.operation == w.CREATE:
                declaration = self.extension.components[request.type_id]
                configuration = decode(declaration.parameters, payload)
                instance = Instance(declaration, declaration.cls(), configuration, asyncio.Lock())
                handle = self.next_handle
                self.next_handle += 1
                self.instances[handle] = instance
                result = w.Response(handle=handle)
            elif stop:
                if self.instances or len(self.calls) != 1: raise RuntimeError("Live instances at STOP")
                result = w.Response()
            else:
                instance = self.instances[request.component]
                async with instance.lock:
                    if request.operation == w.DESTROY:
                        if instance.declaration.kind == "instrumentDriver" and not instance.stopped:
                            raise RuntimeError("Driver destroyed without shutdown")
                        del self.instances[request.component]
                        result = w.Response()
                    elif request.operation == w.INITIALIZE:
                        if instance.initialized or instance.stopped: raise RuntimeError("Invalid initialize state")
                        result = response(await instance.value.initialize(instance.configuration, context))
                        instance.initialized = result.status == w.OK
                    elif request.operation == w.SHUTDOWN:
                        # Stop once, including after partial initialization. Cleanup gets its
                        # own bounded deadline and no inherited cancellation request.
                        if instance.stopped: result = w.Response()
                        else:
                            instance.stopped = True
                            result = response(await instance.value.shutdown(context))
                    elif request.operation == w.INVOKE:
                        context.check_cancelled()
                        if instance.declaration.kind == "command":
                            if request.operation_id == "artest.component.validate.v1":
                                decode(instance.declaration.parameters, payload["parameters"])
                                result = w.Response()
                            elif request.operation_id == "artest.command.execute.v1":
                                parameters = decode(instance.declaration.parameters, payload["parameters"])
                                result = response(await instance.value.execute(parameters, context))
                            else: raise ValueError("Unknown command operation")
                        else:
                            if not instance.initialized or instance.stopped: raise RuntimeError("Driver is not initialized")
                            matches = [(method, getattr(method, "__artest_operation__", None))
                                       for _, method in inspect.getmembers(instance.value, inspect.ismethod)]
                            handler = next(((method, spec[1]) for method, spec in matches
                                            if spec and spec[0] == request.operation_id), None)
                            if handler is None: raise ValueError("Unknown driver operation")
                            result = response(await handler[0](decode(handler[1], payload), context))
                    else: raise ValueError("Unsupported request")
        except OperationError as error: result = response(error.result)
        except (ValueError, TypeError, KeyError) as error:
            result = w.Response(status=w.INVALID_ARGUMENT, diagnostic=(type(error).__name__ + ": " + str(error))[:1000])
        except Exception as error:
            result = w.Response(status=w.EXTENSION_FAILURE, diagnostic=(type(error).__name__ + ": " + str(error))[:1000])
        try:
            reply = self.transport.envelope(envelope.correlation)
            reply.response.CopyFrom(result)
            self.transport.send(reply)
            if stop and result.status == w.OK: self.done.set()
        except BaseException as error: self.fail(error)
        finally:
            self.calls.pop(envelope.correlation, None)
            self.completed[envelope.correlation] = cancelled.is_set()
            if len(self.completed) > 64:
                del self.completed[next(iter(self.completed))]

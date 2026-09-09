"""Developer-facing components. Registration inspects declarations, never factories."""
import asyncio
import inspect
import time
from dataclasses import dataclass
from contextlib import asynccontextmanager
from .schema import schema_for

@dataclass(frozen=True)
class Result:
    data: object = None
    schema_id: str = "artest.schema.generic-json.v1"
    status: str = "ok"
    message: str = ""
    effect_indeterminate: bool = False

    def __post_init__(self):
        if type(self.effect_indeterminate) is not bool:
            raise TypeError("effect_indeterminate must be boolean")
        if self.effect_indeterminate and self.status not in ("error", "cancelled", "timedOut", "invalidArgument"):
            raise ValueError("An indeterminate effect requires a non-success status")

    @staticmethod
    def indeterminate(message: str, status: str = "error"):
        """A write may have executed but its acknowledgement was lost."""
        return Result(status=status, message=message, effect_indeterminate=True)

    @staticmethod
    def verdict(passed: bool, data: object, schema_id: str, message: str = ""):
        if type(passed) is not bool or not schema_id: raise ValueError("Explicit verdict and schema required")
        return Result({"verdict": "passed" if passed else "failed", "data": data,
                       "dataSchema": schema_id, "message": message}, "artest.schema.command-result.v1")

    @staticmethod
    def failure(message: str):
        return Result(status="error", message=message)

class OperationError(Exception):
    def __init__(self, result):
        super().__init__(result.message)
        self.result = result

def operation(identity: str, parameters):
    def decorate(method):
        if not inspect.iscoroutinefunction(method): raise TypeError("Operations must be async")
        method.__artest_operation__ = (identity, parameters)
        return method
    return decorate

class Driver:
    async def initialize(self, config, context): return Result()
    async def shutdown(self, context): return Result()

class Command:
    async def execute(self, parameters, context): raise NotImplementedError

class Context:
    """Call-scoped. Do not store after an operation or detach hardware work."""
    def __init__(self, bridge, correlation, cancelled, deadline, instrument_id=""):
        self._bridge, self._correlation = bridge, correlation
        self._cancelled, self._deadline = cancelled, deadline
        self.instrument_id = instrument_id

    def check_cancelled(self):
        if time.monotonic() >= self._deadline:
            raise OperationError(Result(status="timedOut", message="Operation deadline expired"))
        if self._cancelled.is_set():
            raise OperationError(Result(status="cancelled", message="Operation cancelled"))

    async def sleep(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.check_cancelled()
            await asyncio.sleep(min(0.01, max(0, end - time.monotonic())))
        self.check_cancelled()

    async def blocking(self, function, *args):
        # Wait for the real vendor call: cancellation must never release its device
        # lock early. The Engine kills a worker that exceeds the bounded grace.
        task = asyncio.create_task(asyncio.to_thread(function, *args))
        result = await asyncio.shield(task)
        if isinstance(result, Result) and result.effect_indeterminate:
            # Do not replace a vendor's explicit lost-ack signal with a checkpoint.
            raise OperationError(result)
        self.check_cancelled()
        return result

    def log(self, message, severity=0):
        self._bridge.log(self._correlation, message, severity)

    @asynccontextmanager
    async def instrument(self, contract_id, instance_id=None):
        self.check_cancelled()
        response = await self._bridge.service(self, "resolve", contract_id, {"instanceId": instance_id or self.instrument_id})
        service = _Service(self, response)
        try: yield service
        finally: await self._bridge.service(self, "release", "", {}, service.handle)

class _Service:
    def __init__(self, context, handle): self.context, self.handle = context, handle
    async def invoke(self, operation_id, parameters):
        self.context.check_cancelled()
        return await self.context._bridge.service(self.context, "invoke", operation_id, parameters, self.handle)

@dataclass(frozen=True)
class Component:
    kind: str
    type_id: str
    contract_id: str
    cls: type
    parameters: type
    display_name: str
    requires: tuple
    flags: tuple

class Extension:
    def __init__(self, identity, version, display_name, publisher):
        self.identity, self.version, self.display_name, self.publisher = identity, version, display_name, publisher
        self.components = {}

    def _add(self, kind, identity, contract, cls, parameters, name, requires, flags=()):
        if identity in self.components: raise ValueError("Duplicate component: " + identity)
        schema_for(parameters)
        if not issubclass(cls, Command if kind == "command" else Driver): raise TypeError("Invalid component base")
        self.components[identity] = Component(kind, identity, contract, cls, parameters, name, tuple(requires), tuple(flags))
        return self

    def command(self, identity, cls, parameters, *, name, requires=()):
        return self._add("command", identity, "artest.contract.command.v1", cls, parameters, name, requires)

    def driver(self, identity, cls, configuration, *, name, contract, simulated=False):
        return self._add("instrumentDriver", identity, contract, cls, configuration, name, (),
                         ("simulated",) if simulated else ("requiresHardware",))

    def describe(self):
        result = []
        for item in sorted(self.components.values(), key=lambda item: item.type_id):
            role = "parameters" if item.kind == "command" else "configuration"
            result.append({"kind": item.kind, "typeId": item.type_id, "contractId": item.contract_id,
                           "version": self.version, "displayName": item.display_name, "flags": list(item.flags),
                           "requires": [{"contractId": contract, "selection": "configured"} for contract in item.requires],
                           "schemaRole": role, "schemaId": item.type_id + ".input.v1",
                           "schema": schema_for(item.parameters)})
        return {"extensionId": self.identity, "version": self.version, "displayName": self.display_name,
                "publisher": self.publisher, "components": result}

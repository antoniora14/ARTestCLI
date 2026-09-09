"""Run with the prepared venv (-I); tests the installed private host, not source imports."""
import asyncio
import unittest
from types import SimpleNamespace
from artest_host import artest_process_pb2 as wire
from artest_host.worker import Worker, Instance
from artest_sdk import Result


class Transport:
    def __init__(self):
        self.sent = []

    def envelope(self, correlation):
        return wire.Envelope(major=0, minor=2, generation=1, correlation=correlation)

    def send(self, value):
        saved = wire.Envelope()
        saved.CopyFrom(value)
        self.sent.append(saved)


class CancellationOrderingTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.transport = Transport()
        self.worker = Worker(self.transport, SimpleNamespace(describe=lambda: {}))

    async def complete(self, correlation, operation=wire.DESCRIBE, component=0):
        request = self.transport.envelope(correlation)
        request.request.operation = operation
        request.request.component = component
        self.worker.accept(request)
        await asyncio.sleep(0)
        self.assertNotIn(correlation, self.worker.calls)
        self.assertIsNone(self.worker.failure)

    def cancel(self, correlation):
        value = self.transport.envelope(correlation)
        value.cancel.SetInParent()
        self.worker.accept(value)

    async def test_terminal_response_before_cancel_keeps_cleanup_available(self):
        await self.complete(2)
        self.cancel(2)
        self.assertIsNone(self.worker.failure)
        self.assertTrue(self.transport.sent[-1].cancel.acknowledged)
        cleaned = []

        async def shutdown(context):
            cleaned.append(True)
            return Result()

        self.worker.instances[1] = Instance(
            SimpleNamespace(kind="instrumentDriver"), SimpleNamespace(shutdown=shutdown),
            None, asyncio.Lock(), initialized=True)
        await self.complete(4, wire.SHUTDOWN, 1)
        self.assertEqual(cleaned, [True])
        self.assertEqual(self.transport.sent[-1].response.status, wire.OK)

    async def test_duplicate_cancel_is_rejected(self):
        self.worker.calls[2] = asyncio.Event()
        self.cancel(2)
        self.assertTrue(self.worker.calls[2].is_set())
        self.cancel(2)
        self.assertIsInstance(self.worker.failure, ValueError)

    async def test_unknown_cancel_is_rejected(self):
        self.cancel(100)
        self.assertIsInstance(self.worker.failure, ValueError)

    async def test_terminal_history_is_bounded_and_expired_cancel_is_rejected(self):
        for correlation in range(2, 132, 2):
            await self.complete(correlation)
        self.assertEqual(len(self.worker.completed), 64)
        self.cancel(2)
        self.assertIsInstance(self.worker.failure, ValueError)


if __name__ == "__main__":
    unittest.main()

import asyncio
from dataclasses import dataclass
import math
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from artest_sdk import Command, Driver, Extension, Result, operation, parameter
from artest_sdk.schema import decode, schema_for
from artest_sdk.api import Context, OperationError

@dataclass
class Parameters:
    channel: int
    voltage: float = parameter(default=5.0, minimum=0, maximum=30)
class CountingDriver(Driver):
    instances = 0
    def __init__(self): CountingDriver.instances += 1

class MetadataTests(unittest.TestCase):
    def test_generation_does_not_construct(self):
        definition = Extension("com.test.extension", "0.1.0", "test", "test").driver(
            "com.test.driver", CountingDriver, Parameters, name="test", contract="com.test.contract", simulated=True)
        schema = definition.describe()["components"][0]["schema"]
        self.assertEqual(CountingDriver.instances, 0)
        self.assertEqual(schema["required"], ["channel"])
        self.assertEqual(schema["properties"]["voltage"]["default"], 5.0)

    def test_strict_parameters_and_ranges(self):
        for value in ({"channel": True}, {"channel": 1.0}, {"channel": "1"}, {},
                      {"channel": 1, "unknown": 0}, {"channel": 1, "voltage": -1},
                      {"channel": 2**65}, {"channel": 1, "voltage": math.nan}):
            with self.subTest(value=value), self.assertRaises(ValueError): decode(Parameters, value)
        self.assertEqual(decode(Parameters, {"channel": 1}).voltage, 5.0)

    def test_duplicate_registration_fails(self):
        extension = Extension("com.test.extension", "0.1.0", "test", "test")
        extension.driver("com.test.driver", CountingDriver, Parameters, name="test", contract="com.test.contract")
        with self.assertRaises(ValueError):
            extension.driver("com.test.driver", CountingDriver, Parameters, name="test", contract="com.test.contract")

    def test_failed_verdict_is_successful_transport(self):
        result = Result.verdict(False, {"value": 4.2, "minimum": 4.8, "unit": "V"}, "com.test.measurement.v1")
        self.assertEqual(result.status, "ok")
        self.assertEqual(result.data["verdict"], "failed")
        self.assertEqual(result.schema_id, "artest.schema.command-result.v1")

    def test_unsupported_annotations_fail(self):
        @dataclass
        class Bad: value: dict
        with self.assertRaises(TypeError): schema_for(Bad)

    def test_default_factories_are_not_executed(self):
        from dataclasses import field
        @dataclass
        class Bad: values: list[int] = field(default_factory=lambda: self.fail("factory executed"))
        with self.assertRaises(ValueError): schema_for(Bad)

class CancellationTests(unittest.IsolatedAsyncioTestCase):
    async def test_sleep_observes_cancel(self):
        event = asyncio.Event()
        context = Context(None, 1, event, math.inf)
        event.set()
        with self.assertRaises(OperationError) as error: await context.sleep(1)
        self.assertEqual(error.exception.result.status, "cancelled")

    async def test_blocking_call_finishes_before_cancellation_is_returned(self):
        import threading
        started, release = threading.Event(), threading.Event()
        def vendor_call():
            started.set()
            release.wait(1)
        event = asyncio.Event()
        context = Context(None, 1, event, math.inf)
        task = asyncio.create_task(context.blocking(vendor_call))
        while not started.is_set(): await asyncio.sleep(0.001)
        event.set()
        await asyncio.sleep(0.02)
        self.assertFalse(task.done())
        release.set()
        with self.assertRaises(OperationError): await task

if __name__ == "__main__": unittest.main()

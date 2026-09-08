"""Fault injection only. Never publish this package to an instrument catalog."""
import os
import time
from pathlib import Path
from dataclasses import dataclass
from artest_sdk import Driver, Extension, Result, operation, parameter

@dataclass
class Configuration:
    effectFile: str
    mode: str = "crash"
    blockMs: int = parameter(default=30000, minimum=1, maximum=60000)
@dataclass
class Channel:
    channel: int = 1
@dataclass
class Voltage:
    channel: int = 1
    voltage: float = 12.0

class FaultDriver(Driver):
    async def initialize(self, config, context):
        self.config = config
        if config.mode not in ("crash", "blocking"): raise ValueError("Unknown test fault")
        return Result()

    @operation("artest.instrument.power-supply.v1/set-voltage", Voltage)
    async def voltage(self, parameters, context): return Result()

    @operation("artest.instrument.power-supply.v1/turn-on", Channel)
    async def on(self, parameters, context):
        # Persist a simulated side effect before the process is interrupted.
        with Path(self.config.effectFile).open("a", encoding="utf-8") as stream:
            stream.write("effect\n")
            stream.flush()
            os.fsync(stream.fileno())
        if self.config.mode == "crash": os._exit(23)
        await context.blocking(time.sleep, self.config.blockMs / 1000)
        return Result()

    @operation("artest.instrument.power-supply.v1/turn-off", Channel)
    async def off(self, parameters, context): return Result()

    async def shutdown(self, context):
        Path(self.config.effectFile + ".cleanup").write_text("cleanup\n", encoding="utf-8")
        context.log("PY_FAULT_DRIVER_SHUTDOWN")
        return Result()

def define_extension():
    return Extension("com.artest.python.test-faults", "0.1.0", "Test-only process faults", "ARTest tests").driver(
        "com.artest.python.driver.test-faults", FaultDriver, Configuration,
        name="Test-only fault driver", contract="artest.contract.instrument.power-supply.v1", simulated=True)

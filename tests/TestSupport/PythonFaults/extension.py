"""Fault injection only. Never publish this package to an instrument catalog."""
import os
import time
from pathlib import Path
from dataclasses import dataclass
from artest_sdk import Command, Driver, Extension, Result, operation, parameter
from artest_sdk.api import OperationError

@dataclass
class Configuration:
    effectFile: str
    mode: str = "crash"
    blockMs: int = parameter(default=30000, minimum=1, maximum=60000)
    relay: str = ""
    failShutdown: bool = False
@dataclass
class CommandParameters:
    action: str = "propagate"
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
        if config.mode not in ("crash", "blocking", "lost", "before", "ok", "late",
                               "timeout", "cancelled", "relay", "blocking-lost"):
            raise ValueError("Unknown test fault")
        return Result()

    @operation("artest.instrument.power-supply.v1/set-voltage", Voltage)
    async def voltage(self, parameters, context): return Result()

    @operation("artest.instrument.power-supply.v1/turn-on", Channel)
    async def on(self, parameters, context):
        with Path(self.config.effectFile + ".calls").open("a", encoding="utf-8") as stream:
            stream.write("call\n")
        if self.config.mode == "before": return Result.failure("Confirmed pre-send failure")
        if self.config.mode == "relay":
            async with context.instrument("artest.contract.instrument.power-supply.v1", self.config.relay) as other:
                return await other.invoke("artest.instrument.power-supply.v1/turn-on", {"channel": 1})
        # Persist a simulated side effect before the process is interrupted.
        with Path(self.config.effectFile).open("a", encoding="utf-8") as stream:
            stream.write("effect\n")
            stream.flush()
            os.fsync(stream.fileno())
        if self.config.mode == "crash": os._exit(23)
        if self.config.mode == "ok": return Result()
        if self.config.mode in ("lost", "timeout", "cancelled"):
            return Result.indeterminate("C01 Python lost acknowledgement",
                {"timeout": "timedOut", "cancelled": "cancelled"}.get(self.config.mode, "error"))
        if self.config.mode in ("late", "blocking-lost"):
            def vendor_write():
                time.sleep(0.15)
                return Result.indeterminate("C01 Python lost acknowledgement", "timedOut")
            return await context.blocking(vendor_write)
        await context.blocking(time.sleep, self.config.blockMs / 1000)
        return Result()

    @operation("artest.instrument.power-supply.v1/turn-off", Channel)
    async def off(self, parameters, context): return Result()

    async def shutdown(self, context):
        Path(self.config.effectFile + ".cleanup").write_text("cleanup\n", encoding="utf-8")
        context.log("PY_FAULT_DRIVER_SHUTDOWN")
        return Result.failure("C01 Python cleanup failed") if self.config.failShutdown else Result()

class EffectCommand(Command):
    async def execute(self, parameters, context):
        async with context.instrument("artest.contract.instrument.power-supply.v1") as driver:
            try:
                return await driver.invoke("artest.instrument.power-supply.v1/turn-on", {"channel": 1})
            except OperationError:
                if parameters.action == "wrap": return Result.failure("C01 wrapped Python command failure")
                if parameters.action == "throw": raise RuntimeError("C01 Python command threw after service")
                if parameters.action == "retry":
                    try: await driver.invoke("artest.instrument.power-supply.v1/turn-on", {"channel": 1})
                    except OperationError: pass
                    return Result()
                raise

def define_extension():
    extension = Extension("com.artest.python.test-faults", "0.1.0", "Test-only process faults", "ARTest tests").driver(
        "com.artest.python.driver.test-faults", FaultDriver, Configuration,
        name="Test-only fault driver", contract="artest.contract.instrument.power-supply.v1", simulated=True)
    return extension.command("com.artest.python.command.test-effects", EffectCommand, CommandParameters,
        name="Test-only uncertainty command", requires=("artest.contract.instrument.power-supply.v1",))

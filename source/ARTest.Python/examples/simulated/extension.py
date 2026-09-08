"""No hardware: one power-supply contract shared with the native reference driver."""
from dataclasses import dataclass
from artest_sdk import Command, Driver, Extension, Result, operation, parameter

CONTRACT = "artest.contract.instrument.power-supply.v1"
OP = "artest.instrument.power-supply.v1/"

@dataclass
class Configuration:
    failInitialize: bool = False
    failShutdown: bool = False

@dataclass
class Channel:
    channel: int = parameter(default=1, minimum=0)

@dataclass
class Voltage:
    channel: int = parameter(default=1, minimum=0)
    voltage: float = parameter(default=12.0, minimum=0)

@dataclass
class Measurement:
    channel: int = parameter(default=1, minimum=0)
    voltage: float = parameter(default=4.2, minimum=0)
    minimum: float = parameter(default=4.8, minimum=0)
    holdMs: int = parameter(default=0, minimum=0, maximum=60000)
    fail: bool = False

class SimulatedPower(Driver):
    def __init__(self):
        self.voltages, self.outputs, self.config = {}, {}, Configuration()

    async def initialize(self, config, context):
        self.config = config
        context.log("PY_DRIVER_INITIALIZE")
        if config.failInitialize: return Result.failure("Simulated partial initialization failure")
        return Result()

    @operation(OP + "set-voltage", Voltage)
    async def set_voltage(self, parameters, context):
        context.check_cancelled()
        self.voltages[parameters.channel] = parameters.voltage
        return Result()

    @operation(OP + "turn-on", Channel)
    async def turn_on(self, parameters, context):
        self.outputs[parameters.channel] = True
        return Result()

    @operation(OP + "turn-off", Channel)
    async def turn_off(self, parameters, context):
        self.outputs[parameters.channel] = False
        return Result()

    @operation(OP + "read-state", Channel)
    async def read(self, parameters, context):
        return Result({"channel": parameters.channel, "voltage": self.voltages.get(parameters.channel, 0.0),
                       "outputOn": self.outputs.get(parameters.channel, False)},
                      "artest.schema.instrument.power-supply.result.v1")

    async def shutdown(self, context):
        self.outputs.clear()
        self.voltages.clear()
        context.log("PY_DRIVER_SHUTDOWN")
        return Result.failure("Simulated cleanup failure") if self.config.failShutdown else Result()

class MeasureVoltage(Command):
    async def execute(self, parameters, context):
        if parameters.fail: raise RuntimeError("Simulated command error")
        async with context.instrument(CONTRACT) as power:
            await power.invoke(OP + "set-voltage", {"channel": parameters.channel, "voltage": parameters.voltage})
            await power.invoke(OP + "turn-on", {"channel": parameters.channel})
            await context.sleep(parameters.holdMs / 1000)
            state = await power.invoke(OP + "read-state", {"channel": parameters.channel})
            await power.invoke(OP + "turn-off", {"channel": parameters.channel})
        value = state.data["voltage"]
        context.log("PY_MEASUREMENT_COMPLETED")
        return Result.verdict(value >= parameters.minimum,
            {"value": value, "unit": "V", "minimum": parameters.minimum, "instrumentId": context.instrument_id},
            "artest.schema.measurement.voltage.v1", "Voltage minimum check")

def define_extension():
    extension = Extension("com.artest.python.simulated", "0.1.0", "Python simulated power", "ARTest")
    extension.driver("com.artest.python.driver.power", SimulatedPower, Configuration,
                     name="Python simulated power supply", contract=CONTRACT, simulated=True)
    extension.command("com.artest.python.command.measure-voltage", MeasureVoltage, Measurement,
                      name="Measure voltage", requires=(CONTRACT,))
    return extension

"""Hardware-free driver and broker-based command for a minimal ARTest project."""
from dataclasses import dataclass

from artest_sdk import Command, Driver, Extension, Result, operation, parameter


EXTENSION_ID = "__ARTEST_EXTENSION_ID__"
DRIVER_ID = "__ARTEST_DRIVER_ID__"
COMMAND_ID = "__ARTEST_COMMAND_ID__"
CONTRACT = "__ARTEST_CONTRACT_ID__"
READ_OPERATION = CONTRACT + "/read"


@dataclass
class Configuration:
    value: float = 5.0


@dataclass
class ReadParameters:
    channel: int = parameter(default=1, minimum=0)


@dataclass
class MeasurementParameters:
    channel: int = parameter(default=1, minimum=0)
    minimum: float = 4.8


class SimulatedSource(Driver):
    def __init__(self):
        self.value = 0.0

    async def initialize(self, config, context):
        self.value = config.value
        context.log("MINIMAL_DRIVER_INITIALIZE")
        return Result()

    @operation(READ_OPERATION, ReadParameters)
    async def read(self, parameters, context):
        context.check_cancelled()
        return Result(
            {"channel": parameters.channel, "value": self.value},
            "__ARTEST_READ_SCHEMA_ID__",
        )

    async def shutdown(self, context):
        self.value = 0.0
        context.log("MINIMAL_DRIVER_SHUTDOWN")
        return Result()


class MeasureValue(Command):
    async def execute(self, parameters, context):
        async with context.instrument(CONTRACT) as source:
            measurement = await source.invoke(READ_OPERATION, {"channel": parameters.channel})
        value = measurement.data["value"]
        return Result.verdict(
            value >= parameters.minimum,
            {"value": value, "minimum": parameters.minimum, "instrumentId": context.instrument_id},
            "__ARTEST_MEASUREMENT_SCHEMA_ID__",
            "Minimum simulated value check",
        )


def define_extension():
    extension = Extension(EXTENSION_ID, "0.1.0", "Minimal simulated source", "__ARTEST_AUTHOR__")

    extension.driver(
        DRIVER_ID,
        SimulatedSource,
        Configuration,
        name="Simulated source",
        contract=CONTRACT,
        simulated=True,
    )

    extension.command(
        COMMAND_ID,
        MeasureValue,
        MeasurementParameters,
        name="Measure simulated value",
        requires=(CONTRACT,),
    )

    return extension

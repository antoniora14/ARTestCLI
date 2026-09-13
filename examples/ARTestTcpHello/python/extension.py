"""Same command semantics as C++; transport belongs exclusively to the native driver."""
from dataclasses import dataclass
from artest_sdk import Command, Extension, Result, parameter

CONTRACT = "artest.contract.example.tcp-voltage.v1"

@dataclass
class Measurement:
    voltage: float = parameter(default=12.0, minimum=0, maximum=60)
    minimum: float = parameter(default=11.9, minimum=0, maximum=60)
    maximum: float = parameter(default=12.1, minimum=0, maximum=60)

class SetAndMeasure(Command):
    async def execute(self, parameters, context):
        if parameters.minimum > parameters.maximum:
            return Result(status="invalidArgument", message="minimum must not exceed maximum")
        async with context.instrument(CONTRACT) as instrument:
            result = await instrument.invoke("artest.example.tcp-voltage.v1/apply",
                                             {"voltage": parameters.voltage})
        value = result.data["value"]
        return Result.verdict(parameters.minimum <= value <= parameters.maximum,
            {"value": value, "unit": "V", "minimum": parameters.minimum,
             "maximum": parameters.maximum, "instrumentId": context.instrument_id},
            "artest.schema.example.tcp-measurement.v1", "TCP voltage limits")

def define_extension():
    extension = Extension("com.artest.example.python.tcp-hello", "0.1.0", "Python TCP Hello command", "ARTest")
    extension.command("com.artest.example.python.command.tcp-set-and-measure", SetAndMeasure,
                      Measurement, name="Set and measure voltage (Python)", requires=(CONTRACT,))
    return extension

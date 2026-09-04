#include "PowerOnCommand.h"

namespace artest::extensions
{
sdk::Result PowerOnCommand::Validate(const sdk::Parameters &parameters) const
{
    if (parameters.Get<int>("channel") < 0 || parameters.Get<double>("voltage") < 0.0 ||
        parameters.Get<double>("currentLimit") < 0.0)
        return sdk::Result::Failure(sdk::Status::InvalidArgument,
                                    "channel, voltage and currentLimit must be zero or greater.");
    return sdk::Result::Success();
}

sdk::Result PowerOnCommand::Execute(const sdk::Parameters &parameters, sdk::Context &context)
{
    if (auto result = Validate(parameters); !result)
        return result;
    // Preserve ordering: never enable an output after a failed voltage/current configuration.
    for (const auto operation : {"artest.instrument.power-supply.v1/set-voltage",
                                 "artest.instrument.power-supply.v1/set-current-limit",
                                 "artest.instrument.power-supply.v1/turn-on"})
    {
        auto result = context.CallInstrument("artest.contract.instrument.power-supply.v1",
                                             operation, parameters.Raw());
        if (!result)
            return result;
    }
    return sdk::Result::Success("Power On completed.");
}
} // namespace artest::extensions

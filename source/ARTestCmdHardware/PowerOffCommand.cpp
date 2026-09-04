#include "PowerOffCommand.h"

namespace artest::extensions
{
sdk::Result PowerOffCommand::Validate(const sdk::Parameters &parameters) const
{
    if (parameters.Get<int>("channel") < 0)
        return sdk::Result::Failure(sdk::Status::InvalidArgument, "channel must be zero or greater.");
    return sdk::Result::Success();
}

sdk::Result PowerOffCommand::Execute(const sdk::Parameters &parameters, sdk::Context &context)
{
    if (auto result = Validate(parameters); !result)
        return result;
    auto result = context.CallInstrument("artest.contract.instrument.power-supply.v1",
                                         "artest.instrument.power-supply.v1/turn-off", parameters.Raw());
    if (!result)
        return result;
    return sdk::Result::Success("Power Off completed.");
}
} // namespace artest::extensions

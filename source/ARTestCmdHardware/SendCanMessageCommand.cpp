#include "SendCanMessageCommand.h"

namespace artest::extensions
{
sdk::Result SendCanMessageCommand::Validate(const sdk::Parameters &parameters) const
{
    const auto channel = parameters.Get<int>("channel");
    const auto id = parameters.Get<std::string>("id");
    const auto dlc = parameters.Get<int>("dlc");
    const auto data = parameters.Get<sdk::Json>("data");
    if (channel < 0 || id.empty() || dlc < 0 || dlc > 8 || !data.is_array())
        return sdk::Result::Failure(sdk::Status::InvalidArgument, "Invalid CAN command parameters.");
    // Identifier parsing and frame semantics belong to the CAN service, not the command.
    return sdk::Result::Success();
}

sdk::Result SendCanMessageCommand::Execute(const sdk::Parameters &parameters, sdk::Context &context)
{
    if (auto result = Validate(parameters); !result)
        return result;
    auto result = context.CallInstrument("artest.contract.instrument.can.v1",
                                         "artest.instrument.can.v1/send", parameters.Raw());
    if (!result)
        return result;
    return sdk::Result::Success("Send CAN Message completed.");
}
} // namespace artest::extensions

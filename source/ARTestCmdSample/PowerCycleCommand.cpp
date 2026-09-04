#include "PowerCycleCommand.h"
#include <chrono>

namespace artest::extensions
{
using sdk::Context;
using sdk::Parameters;
using sdk::Result;
using sdk::Status;

Result PowerCycleCommand::Validate(const Parameters &parameters) const
{
    const auto channel = parameters.Optional<int>("channel", -1);
    const auto voltage = parameters.Optional<double>("voltage", -1.0);
    const auto holdMs = parameters.Optional<int>("holdMs", 0);
    if (channel < 0 || voltage < 0.0 || holdMs < 0 || holdMs > 60000)
        return Result::Failure(Status::InvalidArgument,
                               "channel, voltage, and holdMs must be within their valid ranges.");
    return Result::Success();
}

Result PowerCycleCommand::Execute(const Parameters &parameters, Context &context)
{
    if (auto result = Validate(parameters); !result)
        return result;
    const auto channel = parameters.Get<int>("channel");
    constexpr auto contract = "artest.contract.instrument.power-supply.v1";
    auto result = context.CallInstrument(contract, "artest.instrument.power-supply.v1/set-voltage",
                                        {{"channel", channel}, {"voltage", parameters.Get<double>("voltage")}});
    if (!result)
        return result;
    result = context.CallInstrument(contract, "artest.instrument.power-supply.v1/turn-on",
                                    {{"channel", channel}});
    if (!result)
        return result;

    result = context.WaitFor(std::chrono::milliseconds{parameters.Optional<int>("holdMs", 0)});
    // Never bypass cancellation/deadlines to call a service or turn a failed wait into success.
    // The Engine owns unconditional driver Shutdown, which clears all simulated outputs.
    if (!result)
        return result;
    result = context.CallInstrument(contract, "artest.instrument.power-supply.v1/turn-off",
                                    {{"channel", channel}});
    if (!result)
        return result;
    context.Log(sdk::LogLevel::Information, "Sample command completed through driver service.");
    return Result::WithData({
        {"message", "Native command invoked the simulated driver successfully."},
        {"instrumentId", std::string{context.InstrumentId()}}, {"channel", channel}});
}
} // namespace artest::extensions

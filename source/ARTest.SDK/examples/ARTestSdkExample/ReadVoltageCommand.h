#pragma once
#include <ARTest/Command.h>

namespace artest::examples
{

class ReadVoltageCommand final : public sdk::Command
{
  public:
    sdk::Result Validate(const sdk::Parameters &parameters) const override
    {
        const auto channel = parameters.Get<int>("channel");
        const auto settleMs = parameters.Optional<int>("settleMs", 0);

        if (channel < 1 || channel > 4 || settleMs < 0 || settleMs > 60000)
        {
            return sdk::Result::Failure(sdk::Status::InvalidArgument, "channel must be 1..4; settleMs must be 0..60000.");
        }

        return sdk::Result::Success();
    }

    sdk::Result Execute(const sdk::Parameters &parameters, sdk::Context &context) override
    {
        if (auto waited = context.WaitFor(std::chrono::milliseconds{parameters.Optional<int>("settleMs", 0)}); !waited)
            return waited;

        auto response = context.CallInstrument("artest.contract.instrument.power-supply.v1",
                                               "artest.instrument.power-supply.v1/read-state",
                                               {{"channel", parameters.Get<int>("channel")}});
        if (!response) return response;
        if (!response.Data())
            return sdk::Result::Failure(sdk::Status::HostFailure, "The power supply returned no measurement.");

        const sdk::Parameters measurement{*response.Data()};
        const auto voltage = measurement.Get<double>("voltage");

        context.Log(sdk::LogLevel::Information, "Voltage measurement completed.");
        return sdk::Result::Success("Measured " + std::to_string(voltage) + " V.");
    }
};

} // namespace artest::examples

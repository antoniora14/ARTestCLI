#pragma once
#include <ARTest/Command.h>
#include <cmath>

namespace artest::tcphello
{
class SetAndMeasureCommand final : public sdk::Command
{
public:
    sdk::Result Validate(const sdk::Parameters& p) const override
    {
        const auto voltage = p.Get<double>("voltage");
        const auto minimum = p.Get<double>("minimum");
        const auto maximum = p.Get<double>("maximum");
        if (!std::isfinite(voltage) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
            voltage < 0 || voltage > 60 || minimum < 0 || maximum > 60 || minimum > maximum)
            return sdk::Result::Failure(sdk::Status::InvalidArgument, "Require 0 <= minimum <= maximum <= 60 and voltage within 0..60.");
        return sdk::Result::Success();
    }
    sdk::Result Execute(const sdk::Parameters& p, sdk::Context& context) override
    {
        // The command knows a capability, never a DLL class, socket or device address.
        auto response = context.CallInstrument("artest.contract.example.tcp-voltage.v1",
            "artest.example.tcp-voltage.v1/apply", {{"voltage", p.Get<double>("voltage")}});
        if (!response) return response;
        if (!response.Data() || response.SchemaId() != "artest.schema.example.tcp-voltage.v1")
            return sdk::Result::Failure(sdk::Status::HostFailure, "Missing typed voltage response.");
        const double value = response.Data()->at("value").get<double>();
        const double minimum = p.Get<double>("minimum"), maximum = p.Get<double>("maximum");
        return sdk::Result::TestVerdict(value >= minimum && value <= maximum,
            {{"value", value}, {"unit", "V"}, {"minimum", minimum}, {"maximum", maximum},
             {"instrumentId", context.InstrumentId()}},
            "artest.schema.example.tcp-measurement.v1", "TCP voltage limits");
    }
};
} // namespace artest::tcphello

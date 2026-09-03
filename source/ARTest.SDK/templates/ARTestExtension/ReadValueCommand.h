#pragma once
#include <ARTest/Command.h>

namespace artest_extension
{
class ReadValueCommand final : public artest::sdk::Command
{
  public:
    artest::sdk::Result Validate(const artest::sdk::Parameters &parameters) const override
    {
        const auto factor = parameters.Optional<double>("factor", 1.0);
        return factor >= 0.0 && factor <= 10.0
                   ? artest::sdk::Result::Success()
                   : artest::sdk::Result::Failure(
                         artest::sdk::Status::InvalidArgument, "factor must be 0..10.");
    }

    artest::sdk::Result Execute(const artest::sdk::Parameters &parameters,
                                artest::sdk::Context &context) override
    {
        auto response = context.CallInstrument(
            "com.example.artest.contract.value-source.v1",
            "com.example.artest.instrument.value-source.v1/read");
        if (!response)
            return response;
        if (!response.Data())
            return artest::sdk::Result::Failure(artest::sdk::Status::HostFailure,
                                                "The value source returned no data.");

        const artest::sdk::Parameters result{*response.Data()};
        const auto value =
            result.Get<double>("value") * parameters.Optional<double>("factor", 1.0);
        return artest::sdk::Result::Success("Computed value " + std::to_string(value) + ".");
    }
};
} // namespace artest_extension

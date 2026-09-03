#pragma once
#include <ARTest/InstrumentDriver.h>

namespace artest_extension
{
class SimulatedValueSource final : public artest::sdk::InstrumentDriver
{
  public:
    SimulatedValueSource()
    {
        RegisterOperation(
            "com.example.artest.instrument.value-source.v1/read",
            [this](const artest::sdk::Parameters &, artest::sdk::Context &) {
                return artest::sdk::Result::WithData({{"value", m_value}});
            });
    }

    artest::sdk::Result Initialize(const artest::sdk::Parameters &configuration,
                                   artest::sdk::Context &context) override
    {
        m_value = configuration.Optional<double>("initialValue", 42.0);
        context.Log(artest::sdk::LogLevel::Information,
                    "Simulated value source initialized.");
        return artest::sdk::Result::Success();
    }

    artest::sdk::Result Shutdown(artest::sdk::Context &context) override
    {
        m_value = 0.0;
        context.Log(artest::sdk::LogLevel::Information,
                    "Simulated value source shut down.");
        return artest::sdk::Result::Success();
    }

  private:
    double m_value = 0.0;
};
} // namespace artest_extension

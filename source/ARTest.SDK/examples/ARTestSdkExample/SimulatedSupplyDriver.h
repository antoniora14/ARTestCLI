#pragma once
#include <ARTest/InstrumentDriver.h>

namespace artest::examples
{

class SimulatedSupplyDriver final : public sdk::InstrumentDriver
{
private:
    double m_voltage = 0;

  public:
    SimulatedSupplyDriver()
    {
        RegisterOperation("artest.instrument.power-supply.v1/read-state",
                          [this](const sdk::Parameters &parameters, sdk::Context &)
                          {
                              const auto channel = parameters.Get<int>("channel");
                              if (channel < 1 || channel > 4)
                                  return sdk::Result::Failure(sdk::Status::InvalidArgument, "channel must be 1..4.");

                              return sdk::Result::WithData({{"channel", channel}, {"voltage", m_voltage}});
                          });
    }

    sdk::Result Initialize(const sdk::Parameters &configuration, sdk::Context &context) override
    {
        m_voltage = configuration.Optional<double>("voltage", 12.0);
        if (m_voltage < 0 || m_voltage > 60)
            return sdk::Result::Failure(sdk::Status::InvalidArgument, "voltage must be 0..60.");

        context.Log(sdk::LogLevel::Information, "SDK simulated power supply initialized.");
        return sdk::Result::Success();
    }

    sdk::Result Shutdown(sdk::Context &context) override
    {
        m_voltage = 0;
        context.Log(sdk::LogLevel::Information, "SDK simulated power supply shut down.");
        return sdk::Result::Success();
    }
};

} // namespace artest::examples

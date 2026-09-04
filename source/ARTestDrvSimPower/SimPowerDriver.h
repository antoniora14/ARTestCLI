#pragma once

#include <ARTest/InstrumentDriver.h>
#include <map>

namespace artest::extensions
{
// The SDK owns lifecycle state. This class owns only simulated instrument state.
class SimPowerDriver : public sdk::InstrumentDriver
{
  public:
    SimPowerDriver();
    sdk::Result Initialize(const sdk::Parameters &configuration, sdk::Context &context) override;
    sdk::Result Shutdown(sdk::Context &context) override;

  protected:
    explicit SimPowerDriver(bool requireResource);

  private:
    sdk::Result SetVoltage(const sdk::Parameters &parameters);
    sdk::Result SetCurrentLimit(const sdk::Parameters &parameters);
    sdk::Result TurnOn(const sdk::Parameters &parameters);
    sdk::Result TurnOff(const sdk::Parameters &parameters);
    sdk::Result ReadState(const sdk::Parameters &parameters) const;

    bool m_requireResource;
    bool m_failShutdown = false;
    int m_remainingTurnOnFailures = 0;
    std::map<int, double> m_voltages;
    std::map<int, bool> m_outputs;
};

// Keep the legacy type's resource requirement without duplicating the driver.
class LegacySimPowerDriver final : public SimPowerDriver
{
  public:
    LegacySimPowerDriver() : SimPowerDriver(true) {}
};
} // namespace artest::extensions

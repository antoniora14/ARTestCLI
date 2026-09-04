#include "SimPowerDriver.h"
#include <ARTest/Extension.h>

namespace
{
artest::sdk::Extension DefineExtension()
{
    artest::sdk::Extension extension{"com.artest.extension.sim-power", "0.1.0"};
    extension.AddDriver<artest::extensions::SimPowerDriver>(
        {.id = "com.artest.driver.sim.power", .name = "ARTest Simulated Power Supply",
         .contract = "artest.contract.instrument.power-supply.v1",
         .mode = artest::sdk::DriverMode::Simulated});
    extension.AddDriver<artest::extensions::LegacySimPowerDriver>(
        {.id = "com.artest.driver.sim.power-legacy", .name = "ARTest Simulated Power Supply",
         .contract = "artest.contract.instrument.power-supply.v1",
         .mode = artest::sdk::DriverMode::Simulated});
    return extension;
}
} // namespace

ARTEST_EXPORT_EXTENSION(DefineExtension)

#include "SimCanDriver.h"
#include <ARTest/Extension.h>

namespace
{
artest::sdk::Extension DefineExtension()
{
    artest::sdk::Extension extension{"com.artest.extension.sim-can", "0.1.0"};

    extension.AddDriver<artest::extensions::SimCanDriver>(
    {
        .id         = "com.artest.driver.sim.can",
        .name       = "Simulated CAN",
        .contract   = "artest.contract.instrument.can.v1",
        .mode       = artest::sdk::DriverMode::Simulated
    });

    return extension;
}
} // namespace

ARTEST_EXPORT_EXTENSION(DefineExtension)

#include "ReadVoltageCommand.h"
#include "SimulatedSupplyDriver.h"
#include <ARTest/Extension.h>

namespace
{
artest::sdk::Extension DescribeExtension()
{
    artest::sdk::Extension extension{"com.artest.extension.sdk-example", "0.1.0"};

    extension.AddCommand<artest::examples::ReadVoltageCommand>(
        {.id = "com.artest.command.sdk.read-voltage", .name = "Read voltage"});

    extension.AddDriver<artest::examples::SimulatedSupplyDriver>(
        {.id = "com.artest.driver.sdk.sim-supply",
         .name = "SDK simulated supply",
         .contract = "artest.contract.instrument.power-supply.v1",
         .mode = artest::sdk::DriverMode::Simulated});

    return extension;
}
} // namespace

ARTEST_EXPORT_EXTENSION(DescribeExtension)

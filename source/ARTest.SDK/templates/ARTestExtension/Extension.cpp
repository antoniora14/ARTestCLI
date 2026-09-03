#include "ReadValueCommand.h"
#include "SimulatedValueSource.h"
#include <ARTest/Extension.h>

namespace
{
artest::sdk::Extension DefineExtension()
{
    artest::sdk::Extension extension{"com.example.artest.extension.starter", "0.1.0"};
    extension.AddCommand<artest_extension::ReadValueCommand>(
        {.id = "com.example.artest.command.read-value", .name = "Read value"});
    extension.AddDriver<artest_extension::SimulatedValueSource>(
        {.id = "com.example.artest.driver.sim-value-source",
         .name = "Simulated value source",
         .contract = "com.example.artest.contract.value-source.v1",
         .mode = artest::sdk::DriverMode::Simulated});
    return extension;
}
} // namespace

ARTEST_EXPORT_EXTENSION(DefineExtension)

#include "ReadVoltageCommand.h"
#include "SimulatedSupplyDriver.h"
#if defined(ARTEST_METADATA_GENERATOR)
#include <ARTest/MetadataGenerator.h>
#else
#include <ARTest/Extension.h>
#endif

namespace
{
artest::sdk::Extension DescribeExtension()
{
    using artest::sdk::Schema;

    artest::sdk::Extension extension{"com.artest.extension.sdk-example", 
                                     "0.1.0",
                                     "SDK authoring example", 
                                     "ARTest"};

    extension.AddCommand<artest::examples::ReadVoltageCommand>(
    {
        .id = "com.artest.command.sdk.read-voltage", .name = "Read voltage",
        .metadata = {
        .schema = Schema::Object()
             .Required("channel", Schema::Integer().Minimum(1).Maximum(4))
             .Optional("settleMs", Schema::Integer().Minimum(0).Maximum(60000)),
        .schemaId = "artest.schema.sdk.read-voltage.parameters.v1",
        .requiredContracts = {"artest.contract.instrument.power-supply.v1"}}
    });

    extension.AddDriver<artest::examples::SimulatedSupplyDriver>(
    {
        .id = "com.artest.driver.sdk.sim-supply",
        .name = "SDK simulated supply",
        .contract = "artest.contract.instrument.power-supply.v1",
        .mode = artest::sdk::DriverMode::Simulated,
        .metadata = {
             .schema = Schema::Object().Optional("voltage", Schema::Number().Minimum(0).Maximum(60)),
             .schemaId = "artest.schema.sdk.sim-supply.configuration.v1"}
    });

    return extension;
}
} // namespace

#if defined(ARTEST_METADATA_GENERATOR)
ARTEST_GENERATE_METADATA(DescribeExtension)
#else
ARTEST_EXPORT_EXTENSION(DescribeExtension)
#endif

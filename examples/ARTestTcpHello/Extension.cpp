#include "TcpVoltageDriver.h"
#include "SetAndMeasureCommand.h"
#if defined(ARTEST_METADATA_GENERATOR)
#include <ARTest/MetadataGenerator.h>
#else
#include <ARTest/Extension.h>
#endif

namespace
{
artest::sdk::Extension DescribeExtension()
{
    using namespace artest::sdk;
    Extension extension{"com.artest.example.tcp-hello", "0.1.0", "TCP Hello World", "ARTest"};

    extension.AddDriver<artest::tcphello::TcpVoltageDriver>({
        .id = "com.artest.example.driver.tcp-voltage", .name = "Loopback voltage driver",
        .contract = artest::tcphello::Contract, .mode = DriverMode::Simulated,
        .metadata = {.schema = Schema::Object()
            .Required("port", Schema::Integer().Minimum(1).Maximum(65535))
            .Optional("initializeMs", Schema::Integer().Minimum(1).Maximum(10000))
            .Optional("operationMs", Schema::Integer().Minimum(1).Maximum(10000))
            .Optional("shutdownMs", Schema::Integer().Minimum(1).Maximum(10000)),
            .schemaId = "artest.schema.example.tcp-voltage.configuration.v1"}});

    extension.AddCommand<artest::tcphello::SetAndMeasureCommand>({
        .id = "com.artest.example.command.tcp-set-and-measure", .name = "Set and measure voltage",
        .metadata = {.schema = Schema::Object()
            .Required("voltage", Schema::Number().Minimum(0).Maximum(60))
            .Required("minimum", Schema::Number().Minimum(0).Maximum(60))
            .Required("maximum", Schema::Number().Minimum(0).Maximum(60)),
            .schemaId = "artest.schema.example.tcp-measurement.parameters.v1",
            .requiredContracts = {artest::tcphello::Contract}}});

    return extension;
}
}
#if defined(ARTEST_METADATA_GENERATOR)
ARTEST_GENERATE_METADATA(DescribeExtension)
#else
ARTEST_EXPORT_EXTENSION(DescribeExtension)
#endif

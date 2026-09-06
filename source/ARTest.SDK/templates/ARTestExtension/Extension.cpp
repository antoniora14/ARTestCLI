#include "ReadValueCommand.h"
#include "SimulatedValueSource.h"
#if defined(ARTEST_METADATA_GENERATOR)
#include <ARTest/MetadataGenerator.h>
#else
#include <ARTest/Extension.h>
#endif

namespace
{
artest::sdk::Extension DefineExtension()
{
    using artest::sdk::Schema;

    // Shared by the DLL descriptor and the metadata-only build executable.
    artest::sdk::Extension extension{"com.example.artest.extension.starter", "0.1.0",
                                     "ARTest extension starter", "Example"};

    extension.AddCommand<artest_extension::ReadValueCommand>(
    {
        .id = "com.example.artest.command.read-value",
        .name = "Read value",
        .metadata = {
            .schema = Schema::Object()
                .Optional("factor", Schema::Number().Minimum(0).Maximum(10)),
            .schemaId = "com.example.artest.schema.read-value.parameters.v1",
            .requiredContracts = {"com.example.artest.contract.value-source.v1"}
        }
    });

    extension.AddDriver<artest_extension::SimulatedValueSource>(
    {
        .id = "com.example.artest.driver.sim-value-source",
        .name = "Simulated value source",
        .contract = "com.example.artest.contract.value-source.v1",
        .mode = artest::sdk::DriverMode::Simulated,
        .metadata = {
            .schema = Schema::Object()
                .Optional("initialValue", Schema::Number().Minimum(-1000000).Maximum(1000000)),
            .schemaId = "com.example.artest.schema.sim-value-source.configuration.v1"
        }
    });

    return extension;
}
} // namespace

#if defined(ARTEST_METADATA_GENERATOR)
ARTEST_GENERATE_METADATA(DefineExtension)
#else
ARTEST_EXPORT_EXTENSION(DefineExtension)
#endif

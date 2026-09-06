#include "PowerCycleCommand.h"
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
    artest::sdk::Extension extension{"com.artest.extension.sample-command", "0.1.0",
                                     "ARTest Sample Command", "ARTest"};

    extension.AddCommand<artest::extensions::PowerCycleCommand>(
    {
        .id = "com.artest.command.sample.power-cycle",
        .name = "Sample Power Cycle",
        .metadata = {
            .schema = Schema::Object()
                .Required("channel", Schema::Integer().Minimum(0).Maximum(2147483647))
                .Required("voltage", Schema::Number().Minimum(0))
                .Optional("holdMs", Schema::Integer().Minimum(0).Maximum(60000)),
            .schemaId = "artest.schema.sample-power-cycle.parameters.v1",
            .requiredContracts = {"artest.contract.instrument.power-supply.v1"}
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

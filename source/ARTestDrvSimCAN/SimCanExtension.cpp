#include "SimCanDriver.h"
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
    artest::sdk::Extension extension{"com.artest.extension.sim-can", "0.1.0",
                                     "ARTestDrvSimCAN", "ARTest"};

    extension.AddDriver<artest::extensions::SimCanDriver>(
    {
        .id = "com.artest.driver.sim.can",
        .name = "Simulated CAN",
        .contract = "artest.contract.instrument.can.v1",
        .mode = artest::sdk::DriverMode::Simulated,
        .metadata = {
            .schema = Schema::Object()
                .Optional("model", Schema::String())
                .Optional("hw-rsrc", Schema::String())
                .Optional("failInitialize", Schema::Boolean())
                .Optional("failShutdown", Schema::Boolean()),
            .schemaId = "artest.schema.sim-can-configuration.v1",
            .aliases = {"CAN"}
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

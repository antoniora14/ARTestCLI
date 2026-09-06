#include "SimPowerDriver.h"
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
    artest::sdk::Extension extension{"com.artest.extension.sim-power", "0.1.0",
                                     "ARTest Simulated Power Driver", "ARTest"};
    // Both types intentionally retain the same configuration contract.
    const auto configuration = Schema::Object()
        .Optional("failShutdown", Schema::Boolean())
        .Optional("failInitialize", Schema::Boolean())
        .Optional("failInitialization", Schema::Boolean())
        .Optional("failTurnOnAttempts", Schema::Integer().Minimum(0).Maximum(2147483647))
        .Optional("model", Schema::String())
        .Optional("hw-rsrc", Schema::String());

    extension.AddDriver<artest::extensions::SimPowerDriver>(
    {
        .id = "com.artest.driver.sim.power",
        .name = "Simulated Power Supply",
        .contract = "artest.contract.instrument.power-supply.v1",
        .mode = artest::sdk::DriverMode::Simulated,
        .metadata = {
            .schema = configuration,
            .schemaId = "artest.schema.sim-power.configuration.v1"
        }
    });

    extension.AddDriver<artest::extensions::LegacySimPowerDriver>(
    {
        .id = "com.artest.driver.sim.power-legacy",
        .name = "Simulated Legacy Power Supply",
        .contract = "artest.contract.instrument.power-supply.v1",
        .mode = artest::sdk::DriverMode::Simulated,
        .metadata = {
            .schema = configuration,
            .schemaId = "artest.schema.sim-power.configuration.v1",
            .aliases = {"PowerSupply"}
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

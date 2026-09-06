#include "PowerOnCommand.h"
#include "PowerOffCommand.h"
#include "SendCanMessageCommand.h"
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
    artest::sdk::Extension extension{"com.artest.extension.hardware-commands", "0.1.0",
                                     "ARTestCmdHardware", "ARTest"};

    extension.AddCommand<artest::extensions::PowerOnCommand>(
    {
        .id = "com.artest.command.power.turn-on",
        .name = "Power On",
        .metadata = {
            .schema = Schema::Object()
                .Required("channel", Schema::Integer().Minimum(0).Maximum(2147483647))
                .Required("voltage", Schema::Number().Minimum(0))
                .Required("currentLimit", Schema::Number().Minimum(0)),
            .schemaId = "artest.schema.power-on.v1",
            .requiredContracts = {"artest.contract.instrument.power-supply.v1"},
            .aliases = {"PowerSupply.TurnOn"}
        }
    });

    extension.AddCommand<artest::extensions::PowerOffCommand>(
    {
        .id = "com.artest.command.power.turn-off",
        .name = "Power Off",
        .metadata = {
            .schema = Schema::Object()
                .Required("channel", Schema::Integer().Minimum(0).Maximum(2147483647)),
            .schemaId = "artest.schema.power-off.v1",
            .requiredContracts = {"artest.contract.instrument.power-supply.v1"},
            .aliases = {"PowerSupply.TurnOff"}
        }
    });

    extension.AddCommand<artest::extensions::SendCanMessageCommand>(
    {
        .id = "com.artest.command.can.send",
        .name = "Send CAN Message",
        .metadata = {
            .schema = Schema::Object()
                .Required("channel", Schema::Integer().Minimum(0).Maximum(2147483647))
                .Required("id", Schema::String().MinLength(1).MaxLength(10))
                .Required("dlc", Schema::Integer().Minimum(0).Maximum(8))
                .Required("data", Schema::Array(Schema::Integer().Minimum(0).Maximum(255)).MinItems(0).MaxItems(8)),
            .schemaId = "artest.schema.can-send.v1",
            .requiredContracts = {"artest.contract.instrument.can.v1"},
            .aliases = {"CAN.SendMessage"}
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

#include <ARTest/Command.h>
#if defined(ARTEST_METADATA_GENERATOR)
#include <ARTest/MetadataGenerator.h>
#else
#include <ARTest/Extension.h>
#endif
namespace
{
// Negative control: the real driver rejects the voltage before invoking send.
// Deliberately invalid service input belongs only in this test package.
class RejectedVoltage final : public artest::sdk::Command
{
public:
    artest::sdk::Result Execute(const artest::sdk::Parameters&, artest::sdk::Context& context) override
    {
        return context.CallInstrument("artest.contract.example.tcp-voltage.v1",
            "artest.example.tcp-voltage.v1/apply", {{"voltage", -1}});
    }
};
artest::sdk::Extension Define()
{
    artest::sdk::Extension extension{"com.artest.test.tcp-negative", "0.1.0", "TCP pre-send negative control", "ARTest"};
    extension.AddCommand<RejectedVoltage>({
        .id = "com.artest.test.command.tcp-negative", .name = "Rejected voltage",
        .metadata = {.schema = artest::sdk::Schema::Object(),
            .schemaId = "artest.schema.test.tcp-negative.v1",
            .requiredContracts = {"artest.contract.example.tcp-voltage.v1"}}});
    return extension;
}
}
#if defined(ARTEST_METADATA_GENERATOR)
ARTEST_GENERATE_METADATA(Define)
#else
ARTEST_EXPORT_EXTENSION(Define)
#endif

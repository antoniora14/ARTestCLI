#include <ARTest/Metadata.h>
#include <ARTest/Extension.h>
#include "ARTestEngine.Core/Catalog/SchemaValidator.h"
#include "TestSupport/SdkHost.h"
#include <gtest/gtest.h>
#include <limits>

using namespace artest::sdk;

namespace
{
artest::OperationResult ValidateValue(const Json &schema, const Json &value)
{
    return artest::SchemaValidator::Validate(schema, value, "parameters");
}
class NeverConstructCommand final : public Command
{
  public:
    NeverConstructCommand() { throw std::runtime_error("Metadata constructed a command."); }
    Result Execute(const Parameters &, Context &) override { return Result::Success(); }
};
class NeverConstructDriver final : public InstrumentDriver
{
  public:
    NeverConstructDriver() { throw std::runtime_error("Metadata constructed a driver."); }
    Result Initialize(const Parameters &, Context &) override { return Result::Success(); }
    Result Shutdown(Context &) override { return Result::Success(); }
};
Extension Describe(bool reversed = false)
{
    Extension definition{"com.test.extension", "1.2.3", "Metadata example", "ARTest"};
    const auto command = [&] {
        definition.AddCommand<NeverConstructCommand>({
            .id = "com.test.command", .name = "Read",
            .metadata = {.schema = Schema::Object().Required("channel", Schema::Integer().Minimum(1).Maximum(4)),
                         .requiredContracts = {"com.test.contract"}, .aliases = {"Legacy.Read"}}});
    };
    const auto driver = [&] {
        definition.AddDriver<NeverConstructDriver>({
            .id = "com.test.driver", .name = "Simulated device", .contract = "com.test.contract",
            .mode = DriverMode::Simulated,
            .metadata = {.schema = Schema::Object().Optional("voltage", Schema::Number().Minimum(0).Maximum(60))}});
    };
    if (reversed) { driver(); command(); } else { command(); driver(); }
    return definition;
}
Extension AbiDefinition() { return Describe(); }
Extension Single(ComponentMetadata metadata, std::string id = "com.test.command")
{
    Extension definition{"com.test.extension", "1.0.0", "Test", "ARTest"};
    definition.AddCommand<NeverConstructCommand>({.id = std::move(id), .name = "Read", .metadata = std::move(metadata)});
    return definition;
}
} // namespace

TEST(SdkMetadataTests, DefinitionFeedsManifestAndAbiWithoutConstructingComponents)
{
    const auto bundle = GenerateMetadata(Describe(), "Example.dll");
    ASSERT_EQ(bundle.manifest["components"].size(), 2U);
    const auto &command = bundle.manifest["components"][0];
    EXPECT_EQ(command["version"], "1.2.3");
    EXPECT_EQ(command["requires"][0]["contractId"], "com.test.contract");
    EXPECT_EQ(command["aliases"][0], "Legacy.Read");
    EXPECT_EQ(bundle.manifest["components"][1]["flags"], Json::array({"simulated"}));
    sdk_tests::Harness<AbiDefinition> abi;
    ASSERT_EQ(abi.api.get_component_type_count(abi.extension), 2U);
    // Existing ABI tests cover full descriptor comparison; constructing either
    // class here would throw and fail generation/query before any device work.
    EXPECT_EQ(bundle.schemas.size(), 2U);
}

TEST(SdkMetadataTests, GeneratedSchemasEnforceTheRealEngineValidationRules)
{
    const auto bundle = GenerateMetadata(Describe(), "Example.dll");
    for (const auto &[path, schema] : bundle.schemas)
    {
        EXPECT_TRUE(artest::SchemaValidator::Check(schema).Succeeded()) << path;
    }
    const auto &schema = bundle.schemas.at("schemas/com.test.command.parameters.json");
    EXPECT_TRUE(ValidateValue(schema, Json{{"channel", 1}}).Succeeded());
    for (const auto &value : std::vector<Json>{
             Json::object(), {{"channel", 0}}, {{"channel", 5}},
             {{"channel", 1.0}}, {{"channel", "1"}}, {{"channel", 1}, {"extra", true}}})
        EXPECT_FALSE(ValidateValue(schema, value).Succeeded()) << value.dump();
}

TEST(SdkMetadataTests, NestedSchemasOwnTheirValuesAndMatchTheProfile)
{
    auto item = Schema::Integer().Minimum(0).Maximum(255);
    const auto schema = Schema::Object()
        .Required("data", Schema::Array(item).MinItems(1).MaxItems(8))
        .Optional("name", Schema::String().MinLength(1).MaxLength(20))
        .Optional("enabled", Schema::Boolean()).Document();
    item.Maximum(1);
    ASSERT_TRUE(artest::SchemaValidator::Check(schema).Succeeded());
    EXPECT_TRUE(ValidateValue(schema, Json{{"data", {255}}}).Succeeded());
    EXPECT_FALSE(ValidateValue(schema, Json{{"data", {256}}}).Succeeded());
    EXPECT_FALSE(ValidateValue(schema, Json{{"data", Json::array()}}).Succeeded());
    EXPECT_FALSE(ValidateValue(schema, Json{{"data", {1}}, {"name", ""}}).Succeeded());
}

TEST(SdkMetadataTests, InvalidSchemaDeclarationsFailBeforePublication)
{
    EXPECT_THROW(Schema::Object().Required("x", Schema::Integer()).Optional("x", Schema::String()), std::invalid_argument);
    EXPECT_THROW(Schema::String().Minimum(0), std::invalid_argument);
    EXPECT_THROW(Schema::Integer().Required("x", Schema::Boolean()), std::invalid_argument);
    EXPECT_THROW(Schema::Number().Maximum(std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW((void)Schema::Number().Minimum(10).Maximum(2).Document(), std::invalid_argument);
    EXPECT_THROW((void)Schema::Array(Schema::Boolean()).MinItems(4).MaxItems(1).Document(), std::invalid_argument);
    EXPECT_THROW((void)Schema::String().MinLength(4).MaxLength(1).Document(), std::invalid_argument);
    auto deep = Schema::Boolean();
    for (int i = 0; i < 34; ++i) deep = Schema::Array(deep);
    EXPECT_THROW((void)deep.Document(), std::invalid_argument);
    EXPECT_THROW((void)Schema::String().Description(std::string(1024 * 1024, 'x')).Document(), std::invalid_argument);
}

TEST(SdkMetadataTests, MissingDeclarationsUnsafeIdsAndPathsAreRejected)
{
    EXPECT_THROW((void)GenerateMetadata(Single({}), "Example.dll"), std::invalid_argument);
    for (const auto name : {"../Example.dll", "C:\\Example.dll", "Example.exe", "sub/Example.dll"})
        EXPECT_THROW((void)GenerateMetadata(Describe(), name), std::invalid_argument);
    for (const auto id : {"../escape", "COM.test", "missingseparator", "com/test"})
        EXPECT_THROW((void)GenerateMetadata(Single({.schema = Schema::Object()}, id), "Example.dll"), std::invalid_argument);
    EXPECT_THROW((void)GenerateMetadata(Single({.schema = Schema::Integer()}), "Example.dll"), std::invalid_argument);
    Extension unnamed{"com.test.extension", "1.0.0"};
    EXPECT_THROW((void)GenerateMetadata(unnamed, "Example.dll"), std::invalid_argument);
    Extension badVersion{"com.test.extension", "1.0", "Test", "ARTest"};
    EXPECT_THROW((void)GenerateMetadata(badVersion, "Example.dll"), std::invalid_argument);
}

TEST(SdkMetadataTests, IdentitySchemaAndRequirementCollisionsAreRejected)
{
    EXPECT_THROW((void)GenerateMetadata(Single({.schema = Schema::Object(),
        .aliases = {"com.test.command"}}), "Example.dll"), std::invalid_argument);
    EXPECT_THROW((void)GenerateMetadata(Single({.schema = Schema::Object(),
        .requiredContracts = {"com.test.contract", "com.test.contract"}}), "Example.dll"), std::invalid_argument);
    auto definition = Describe();
    definition.AddCommand<NeverConstructCommand>({
        .id = "com.test.other", .name = "Other",
        .metadata = {.schema = Schema::Object(), .schemaId = "com.test.command.parameters.v1"}});
    EXPECT_THROW((void)GenerateMetadata(definition, "Example.dll"), std::invalid_argument);
    EXPECT_THROW((void)GenerateMetadata(Single({.schema = Schema::Object(),
        .aliases = {"Alias", "Alias"}}), "Example.dll"), std::invalid_argument);
}

TEST(SdkMetadataTests, GenerationIsDeterministicAndPreservesExplicitSchemaIdentity)
{
    const auto first = GenerateMetadata(Describe(), "Example.dll");
    const auto second = GenerateMetadata(Describe(true), "Example.dll");
    EXPECT_EQ(first.manifest.dump(2), second.manifest.dump(2));
    EXPECT_EQ(first.schemas, second.schemas);
    const auto explicitId = GenerateMetadata(Single({.schema = Schema::Object(),
        .schemaId = "com.test.schema.custom.v2"}), "Example.dll");
    EXPECT_EQ(explicitId.manifest["components"][0]["schemas"][0]["schemaId"], "com.test.schema.custom.v2");
    EXPECT_FALSE(explicitId.manifest.contains("integrity")); // Hashing belongs to packaging.
}

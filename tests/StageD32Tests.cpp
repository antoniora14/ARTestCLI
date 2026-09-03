#include "ARTest.SDK/include/ARTestEngineClient.h"
#include "ARTestEngine.Core/Catalog/ComponentCatalog.h"
#include "ARTestEngine.Core/Catalog/RegistryTransaction.h"
#include "ARTestEngine.Core/Catalog/SchemaValidator.h"
#include "ARTestEngine.Core/Commands/BuiltIn/WaitCommand.h"
#include "TestSupport/Fakes/FakePowerSupply.h"
#include <gtest/gtest.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <atomic>
#include <array>
#include <fstream>
#include <filesystem>

using nlohmann::json;
using artest::sdk::EngineClient;

namespace
{
    std::filesystem::path BinaryDirectory()
    {
        wchar_t value[32768]{};
        GetModuleFileNameW(nullptr, value, 32768);
        return std::filesystem::path{value}.parent_path();
    }
    std::filesystem::path Packages()
    {
        const auto bin = BinaryDirectory();
        return bin.parent_path().parent_path().parent_path()
            / "extensions" / bin.parent_path().filename() / bin.filename();
    }
    json ReadJson(const std::filesystem::path& path)
    {
        std::ifstream input{path};
        return json::parse(input);
    }
    void WriteJson(const std::filesystem::path& path, const json& value)
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << value.dump(2);
    }
    struct PackageFixture
    {
        PackageFixture()
        {
            static std::atomic_uint next{0};
            root = std::filesystem::temp_directory_path()
                / ("ARTest-D32-" + std::to_string(GetCurrentProcessId()) + "-"
                    + std::to_string(GetTickCount64()) + "-" + std::to_string(next++));
            std::filesystem::copy(Packages(), root, std::filesystem::copy_options::recursive);
        }
        ~PackageFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
        std::filesystem::path root;
    };
    json Plan(int failures = 0)
    {
        return {
            {"format", "ARTest.Script"}, {"version", 1},
            {"instruments", json::array({{
                {"type", "PowerSupply"}, {"id", "PS1"},
                {"config", {{"hw-rsrc", "SIM::PS1"}, {"failTurnOnAttempts", failures}}}
            }})},
            {"commands", json::array({{
                {"stepId", 1}, {"name", "PowerSupply.TurnOn"}, {"instrument", "PS1"},
                {"params", {{"channel", 1}, {"voltage", 12.0}, {"currentLimit", 2.0}}},
                {"policy", {{"maxAttempts", 2}, {"onFailure", "stop"}}}
            }})}};
    }
    json Finish(EngineClient& client)
    {
        bool done = false;
        const auto waited = client.Wait(10000, done);
        EXPECT_TRUE(waited.Succeeded()) << waited.message;
        EXPECT_TRUE(done);
        std::string output;
        const auto serialized = client.SerializeResult(output);
        EXPECT_TRUE(serialized.Succeeded()) << serialized.message;
        return output.empty() ? json::object() : json::parse(output);
    }
    void Prepare(EngineClient& client, const std::filesystem::path& path)
    {
        ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
        const auto prepared = client.PrepareCatalog(path.string());
        ASSERT_TRUE(prepared.Succeeded()) << prepared.message;
    }
}

TEST(SchemaProfileTests, RejectsUnknownKeywordsInsteadOfPretendingToValidateThem)
{
    const auto result = artest::SchemaValidator::Check(
        {{"type", "string"}, {"pattern", "unsupported"}});
    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "SCHEMA_KEYWORD_UNSUPPORTED");
}

TEST(SchemaProfileTests, ValidatesRequiredPropertiesRangesAndUnknownProperties)
{
    const json schema = {{"type", "object"}, {"required", {"value"}},
        {"properties", {{"value", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}}}},
        {"additionalProperties", false}};
    EXPECT_TRUE(artest::SchemaValidator::Validate(schema, {{"value", 255}}, "params").Succeeded());
    for (const auto& invalid : std::vector<json>{
        json::object(), {{"value", 256}}, {{"value", -1}}, {{"value", 1.5}},
        {{"value", true}}, {{"value", "1"}}, {{"value", 0}, {"extra", 1}}})
        EXPECT_FALSE(artest::SchemaValidator::Validate(schema, invalid, "params").Succeeded())
            << invalid.dump();
}

TEST(SchemaProfileTests, ValidatesArrayItemsEnumsAndUnicodeLengths)
{
    const json array = {{"type", "array"}, {"minItems", 1}, {"maxItems", 2},
        {"items", {{"type", "integer"}, {"enum", {0, 1}}}}};
    EXPECT_TRUE(artest::SchemaValidator::Validate(array, {0, 1}, "data").Succeeded());
    EXPECT_FALSE(artest::SchemaValidator::Validate(array, {0, 2}, "data").Succeeded());
    EXPECT_FALSE(artest::SchemaValidator::Validate(array, json::array(), "data").Succeeded());
    EXPECT_FALSE(artest::SchemaValidator::Validate(array, {0, 1, 0}, "data").Succeeded());
    const json text = {{"type", "string"}, {"minLength", 1}, {"maxLength", 1}};
    EXPECT_TRUE(artest::SchemaValidator::Validate(text, "\xc3\xa9", "name").Succeeded());
    EXPECT_FALSE(artest::SchemaValidator::Validate(text, "ab", "name").Succeeded());
}

TEST(SchemaProfileTests, RejectsMalformedSchemasAndExcessiveNesting)
{
    for (const auto& invalid : std::vector<json>{
        {{"type", "object"}, {"required", {"x"}}},
        {{"type", "string"}, {"minLength", -1}},
        {{"type", "array"}, {"items", false}},
        {{"type", "boolean"}, {"minimum", 0}},
        {{"type", "integer"}, {"enum", json::array()}}})
        EXPECT_FALSE(artest::SchemaValidator::Check(invalid).Succeeded()) << invalid.dump();
    json nested = {{"type", "integer"}};
    for (int i = 0; i < 34; ++i) nested = {{"type", "array"}, {"items", nested}};
    EXPECT_FALSE(artest::SchemaValidator::Check(nested).Succeeded());
}

TEST(CatalogMetadataTests, AliasConflictsRejectTheWholeMetadataBatch)
{
    artest::ComponentCatalog catalog;
    artest::ComponentDescriptor first;
    first.typeId = "test.first";
    first.aliases = {"Legacy"};
    ASSERT_TRUE(catalog.Add({first}).Succeeded());
    artest::ComponentDescriptor second;
    second.typeId = "test.second";
    second.aliases = {"Legacy"};
    EXPECT_FALSE(catalog.Add({second}).Succeeded());
    EXPECT_EQ(catalog.Find("test.second"), nullptr);
    ASSERT_NE(catalog.Find("Legacy"), nullptr);
    EXPECT_EQ(catalog.Find("Legacy")->typeId, "test.first");
}

TEST(RegistryTransactionTests, ConflictInDriverBatchDoesNotPublishAnyCommands)
{
    artest::CommandRegistry commands;
    artest::InstrumentRegistry instruments;
    ASSERT_TRUE(commands.Register("existing", [] { return std::make_unique<artest::WaitCommand>(); }).Succeeded());
    auto result = artest::RegistryTransaction::Commit(commands, instruments,
        {{"new.command", [] { return std::make_unique<artest::WaitCommand>(); }}},
        {{"existing", [](artest::IEventSink& sink) { return std::make_unique<artest::FakePowerSupply>(sink); }}});
    EXPECT_FALSE(result.Succeeded());
    EXPECT_FALSE(commands.Contains("new.command"));
    EXPECT_TRUE(commands.Contains("existing"));
    EXPECT_FALSE(instruments.Contains("existing"));
}

TEST(RegistryTransactionTests, OwnerTokenRevokesOnlyItsOwnBatch)
{
    artest::CommandRegistry commands;
    artest::InstrumentRegistry instruments;
    const auto factory = [] { return std::make_unique<artest::WaitCommand>(); };
    ASSERT_TRUE(commands.Register("intrinsic", factory).Succeeded());
    auto first = artest::RegistryTransaction::Commit(commands, instruments, {{"first", factory}}, {});
    auto second = artest::RegistryTransaction::Commit(commands, instruments, {{"second", factory}}, {});
    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    artest::RegistryTransaction::Revoke(commands, instruments, *first.value);
    EXPECT_FALSE(commands.Contains("first"));
    EXPECT_TRUE(commands.Contains("second"));
    EXPECT_TRUE(commands.Contains("intrinsic"));
    artest::RegistryTransaction::Revoke(commands, instruments, *first.value);
    EXPECT_TRUE(commands.Contains("second"));
}

TEST(RegistryTransactionTests, FactoriesMayReenterRegistryWithoutDeadlocking)
{
    artest::CommandRegistry commands;
    ASSERT_TRUE(commands.Register("reentrant", [&commands]
    {
        EXPECT_TRUE(commands.Register("nested", [] { return std::make_unique<artest::WaitCommand>(); }).Succeeded());
        return std::make_unique<artest::WaitCommand>();
    }).Succeeded());
    EXPECT_NE(commands.Create("reentrant"), nullptr);
    EXPECT_TRUE(commands.Contains("nested"));
}

TEST(OfflineCompilationTests, CompilesMetadataEvenWhenEntryIsNotALoadableDll)
{
    PackageFixture packages;
    const auto package = packages.root / "ARTestCmdHardware";
    auto manifest = ReadJson(package / "artest-extension.json");
    manifest.erase("integrity");
    WriteJson(package / "artest-extension.json", manifest);
    const auto entry = package / "ARTestCmdHardware.dll";
    { std::ofstream output{entry, std::ios::binary | std::ios::trunc}; output << "Not a native module"; }
    EngineClient client;
    Prepare(client, packages.root);
    const auto compiled = client.Compile(Plan().dump());
    ASSERT_TRUE(compiled.Succeeded()) << compiled.message;
    EXPECT_EQ(GetModuleHandleW(entry.c_str()), nullptr);
    ASSERT_TRUE(client.Start().Succeeded());
    const auto report = Finish(client);
    EXPECT_EQ(report["status"], "error");
    EXPECT_EQ(report["summary"]["executedSteps"], 0);
    EXPECT_NE(report.dump().find("EXTENSION_LOAD_FAILED"), std::string::npos);
    std::string snapshot;
    ASSERT_TRUE(client.GetCatalogSnapshot(snapshot).Succeeded());
    EXPECT_EQ(json::parse(snapshot)["status"], "rejected");
}

TEST(OfflineCompilationTests, InvalidParametersFailBeforeAnyDllIsLoaded)
{
    PackageFixture packages;
    EngineClient client;
    Prepare(client, packages.root);
    auto plan = Plan();
    plan["commands"][0]["params"]["voltage"] = -1;
    const auto result = client.Compile(plan.dump());
    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.message.find("PARAMETER_RANGE_INVALID"), std::string::npos);
    EXPECT_EQ(GetModuleHandleW((packages.root / "ARTestCmdHardware/ARTestCmdHardware.dll").c_str()), nullptr);
}

TEST(OfflineCompilationTests, RejectsWrongConfiguredDriverContract)
{
    PackageFixture packages;
    EngineClient client;
    Prepare(client, packages.root);
    auto plan = Plan();
    plan["instruments"][0]["type"] = "CAN";
    plan["instruments"][0]["config"].erase("failTurnOnAttempts");
    const auto result = client.Compile(plan.dump());
    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.message.find("COMMAND_INSTRUMENT_CONTRACT_MISMATCH"), std::string::npos);
}

TEST(OfflineCompilationTests, RejectsUnsupportedSchemasDuringPreparation)
{
    PackageFixture packages;
    const auto path = packages.root / "ARTestCmdHardware/schemas/power-on.json";
    auto schema = ReadJson(path);
    schema["$ref"] = "https://untrusted.invalid/schema";
    WriteJson(path, schema);
    EngineClient client;
    ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    const auto result = client.PrepareCatalog(packages.root.string());
    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.message.find("SCHEMA_KEYWORD_UNSUPPORTED"), std::string::npos);
}

TEST(OfflineCompilationTests, AliasAndCanonicalIdHaveIdenticalSemantics)
{
    PackageFixture packages;
    EngineClient client;
    Prepare(client, packages.root);
    auto plan = Plan();
    ASSERT_TRUE(client.Compile(plan.dump()).Succeeded());
    plan["commands"][0]["name"] = "com.artest.command.power.turn-on";
    plan["instruments"][0]["type"] = "com.artest.driver.sim.power-legacy";
    ASSERT_TRUE(client.Compile(plan.dump()).Succeeded());
    ASSERT_TRUE(client.Start().Succeeded());
    EXPECT_EQ(Finish(client)["status"], "passed");
}

TEST(PreparedPlanTests, ChangedSchemaInvalidatesActivationOfAnAlreadyCompiledPlan)
{
    PackageFixture packages;
    EngineClient client;
    Prepare(client, packages.root);
    ASSERT_TRUE(client.Compile(Plan().dump()).Succeeded());
    const auto path = packages.root / "ARTestCmdHardware/schemas/power-on.json";
    auto schema = ReadJson(path);
    schema["description"] = "Changed after compilation";
    WriteJson(path, schema);
    ASSERT_TRUE(client.Start().Succeeded());
    const auto report = Finish(client);
    EXPECT_EQ(report["status"], "error");
    EXPECT_EQ(report["summary"]["executedSteps"], 0);
    EXPECT_NE(report.dump().find("EXTENSION_CATALOG_CHANGED"), std::string::npos);
}

TEST(PreparedPlanTests, PreparingAnotherRevisionMakesExistingPlansStale)
{
    PackageFixture packages;
    EngineClient client;
    Prepare(client, packages.root);
    ASSERT_TRUE(client.Compile(Plan().dump()).Succeeded());
    ASSERT_TRUE(client.PrepareCatalog(packages.root.string()).Succeeded());
    EXPECT_EQ(client.Start().code, ARTEST_STATUS_INVALID_STATE);
}

TEST(PreparedPlanTests, RepeatedRunsUseFreshDriverAndCommandInstances)
{
    PackageFixture packages;
    EngineClient client;
    Prepare(client, packages.root);
    ASSERT_TRUE(client.Compile(Plan(1).dump()).Succeeded());
    ASSERT_TRUE(client.Start().Succeeded());
    auto first = Finish(client);
    ASSERT_EQ(first["status"], "passed") << first.dump();
    EXPECT_EQ(first["summary"]["totalAttempts"], 2);
    ASSERT_TRUE(client.Restart().Succeeded());
    auto second = Finish(client);
    EXPECT_EQ(second["status"], "passed");
    EXPECT_EQ(second["summary"]["totalAttempts"], 2);
}

TEST(PreparedPlanTests, FailedDriverInitializationStillAttemptsShutdown)
{
    PackageFixture packages;
    EngineClient client;
    Prepare(client, packages.root);
    std::string events;
    ASSERT_TRUE(client.SubscribeEvents([&events](std::string_view text) { events += text; }).Succeeded());
    auto plan = Plan();
    plan["instruments"][0]["config"]["failInitialize"] = true;
    ASSERT_TRUE(client.Compile(plan.dump()).Succeeded());
    ASSERT_TRUE(client.Start().Succeeded());
    const auto report = Finish(client);
    EXPECT_EQ(report["status"], "error");
    EXPECT_EQ(report["summary"]["executedSteps"], 0);
    EXPECT_NE(events.find("shut down"), std::string::npos);
}

TEST(PreparedPlanTests, ExtensionEventsMayInspectCatalogWithoutDeadlocking)
{
    PackageFixture packages;
    EngineClient client;
    Prepare(client, packages.root);
    std::atomic_uint observations{0};
    ASSERT_TRUE(client.SubscribeEvents([&](std::string_view)
    {
        std::string snapshot;
        if (client.GetCatalogSnapshot(snapshot).Succeeded()) ++observations;
    }).Succeeded());
    ASSERT_TRUE(client.Compile(Plan().dump()).Succeeded());
    ASSERT_TRUE(client.Start().Succeeded());
    EXPECT_EQ(Finish(client)["status"], "passed");
    EXPECT_GT(observations.load(), 0U);
}

TEST(StageD32AbiTests, MinorThreeNegotiationPreservesTheAppendedFieldSentinel)
{
    alignas(ARTestEngineApiV0) std::array<unsigned char, sizeof(ARTestEngineApiV0)> storage;
    storage.fill(0xa5U);
    auto* api = reinterpret_cast<ARTestEngineApiV0*>(storage.data());
    api->struct_size = ARTEST_ENGINE_API_V0_3_SIZE;
    ASSERT_EQ(ARTestEngine_QueryApi(0, 3, api, nullptr), ARTEST_STATUS_OK);
    EXPECT_EQ(api->api_minor, 3U);
    EXPECT_NE(api->validate_catalog, nullptr);
    EXPECT_TRUE(std::all_of(storage.begin() + ARTEST_ENGINE_API_V0_3_SIZE,
        storage.end(), [](unsigned char byte) { return byte == 0xa5U; }));
    storage.fill(0xa5U);
    api->struct_size = sizeof(ARTestEngineApiV0);
    ASSERT_EQ(ARTestEngine_QueryApi(0, 3, api, nullptr), ARTEST_STATUS_OK);
    EXPECT_TRUE(std::all_of(storage.begin() + ARTEST_ENGINE_API_V0_3_SIZE,
        storage.end(), [](unsigned char byte) { return byte == 0xa5U; }));
}

TEST(OfflineCompilationTests, MissingLegacyDriverSchemaCannotBypassConfigurationValidation)
{
    PackageFixture packages;
    const auto path = packages.root / "ARTestDrvSimPower/artest-extension.json";
    auto manifest = ReadJson(path);
    manifest["schemaVersion"] = 1;
    for (auto& component : manifest["components"])
    {
        component.erase("schemas");
        component.erase("aliases");
    }
    WriteJson(path, manifest);
    EngineClient client;
    Prepare(client, packages.root);
    auto plan = Plan();
    plan["instruments"][0]["type"] = "com.artest.driver.sim.power";
    const auto result = client.Compile(plan.dump());
    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.message.find("INSTRUMENT_SCHEMA_MISSING"), std::string::npos);
}

TEST(PreparedPlanTests, IntrinsicAliasCollisionRejectsPreparationAndReportsInvalid)
{
    PackageFixture packages;
    const auto path = packages.root / "ARTestCmdHardware/artest-extension.json";
    auto manifest = ReadJson(path);
    manifest["components"][0]["aliases"].push_back("Time.WaitMs");
    WriteJson(path, manifest);
    EngineClient client;
    ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    const auto result = client.PrepareCatalog(packages.root.string());
    EXPECT_FALSE(result.Succeeded());
    EXPECT_NE(result.message.find("EXTENSION_COMPONENT_DUPLICATE"), std::string::npos);
    std::string report;
    ASSERT_TRUE(client.GetCatalogSnapshot(report).Succeeded());
    EXPECT_FALSE(json::parse(report)["valid"].get<bool>());
    EXPECT_EQ(json::parse(report)["status"], "rejected");
}

TEST(PreparedPlanTests, NativeManifestVersionMustMatchTheBinary)
{
    PackageFixture packages;
    const auto path = packages.root / "ARTestCmdHardware/artest-extension.json";
    auto manifest = ReadJson(path);
    manifest["version"] = "9.9.9";
    WriteJson(path, manifest);
    EngineClient client;
    Prepare(client, packages.root);
    ASSERT_TRUE(client.Compile(Plan().dump()).Succeeded());
    ASSERT_TRUE(client.Start().Succeeded());
    const auto result = Finish(client);
    EXPECT_EQ(result["status"], "error");
    EXPECT_NE(result.dump().find("EXTENSION_VERSION_MISMATCH"), std::string::npos);
}

TEST(OfflineCompilationTests, UnrecognizedFlagsAndWrongManifestVersionTypesFailClosed)
{
    PackageFixture packages;
    const auto path = packages.root / "ARTestCmdHardware/artest-extension.json";
    auto original = ReadJson(path);
    for (int scenario = 0; scenario < 2; ++scenario)
    {
        auto manifest = original;
        if (scenario == 0) manifest["components"][0]["flags"] = {"unknownFlag"};
        else manifest["schemaVersion"] = "2";
        WriteJson(path, manifest);
        EngineClient client;
        ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
        const auto result = client.PrepareCatalog(packages.root.string());
        EXPECT_FALSE(result.Succeeded());
        EXPECT_NE(result.message.find(scenario == 0 ? "EXTENSION_FLAG_INVALID"
            : "EXTENSION_SCHEMA_VERSION_UNSUPPORTED"), std::string::npos);
    }
}

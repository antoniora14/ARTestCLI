#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ARTestEngineClient.h>
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <set>
#include "TestSupport/ReferenceCatalog.h"

using artest::sdk::EngineClient;
using Json = nlohmann::json;

namespace
{
std::filesystem::path Packages()
{
    wchar_t buffer[32768]{};
    GetModuleFileNameW(nullptr, buffer, 32768);
    const auto bin = std::filesystem::path{buffer}.parent_path();
    return bin.parent_path().parent_path().parent_path() / "extensions" /
           bin.parent_path().filename() / bin.filename();
}
Json Plan(bool aliases)
{
    return {
        {"format", "ARTest.Script"}, {"version", 1},
        {"instruments", Json::array({
            {{"type", aliases ? "PowerSupply" : "com.artest.driver.sim.power-legacy"}, {"id", "PS1"},
             {"config", {{"hw-rsrc", "SIM::PS1"}}}},
            {{"type", aliases ? "CAN" : "com.artest.driver.sim.can"}, {"id", "CAN1"},
             {"config", {{"hw-rsrc", "SIM::CAN1"}}}}})},
        {"commands", Json::array({
            {{"stepId", 1}, {"name", aliases ? "PowerSupply.TurnOn" : "com.artest.command.power.turn-on"},
             {"instrument", "PS1"}, {"params", {{"channel", 1}, {"voltage", 12.0}, {"currentLimit", 2.0}}}},
            {{"stepId", 2}, {"name", aliases ? "CAN.SendMessage" : "com.artest.command.can.send"},
             {"instrument", "CAN1"}, {"params", {{"channel", 0}, {"id", "0x123"}, {"dlc", 2}, {"data", {0, 255}}}}},
            {{"stepId", 3}, {"name", aliases ? "PowerSupply.TurnOff" : "com.artest.command.power.turn-off"},
             {"instrument", "PS1"}, {"params", {{"channel", 1}}}},
            {{"stepId", 4}, {"name", "com.artest.command.sample.power-cycle"},
             {"instrument", "PS1"}, {"params", {{"channel", 1}, {"voltage", 5}, {"holdMs", 0}}}}})}};
}

class ReferenceEngineTests : public ::testing::Test
{
  protected:
    // Keep files alive until Engine destruction releases its module handles.
    artest::tests::ReferenceCatalog referenceCatalog{Packages()};
    EngineClient client;
    void SetUp() override
    {
        ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
        const auto prepared = client.PrepareCatalog(referenceCatalog.Root().string());
        ASSERT_TRUE(prepared.Succeeded()) << prepared.message;
    }
    Json Run(const Json &plan)
    {
        const auto compiled = client.Compile(plan.dump());
        EXPECT_TRUE(compiled.Succeeded()) << compiled.message;
        if (!compiled.Succeeded()) return Json::object();
        const auto started = client.Start();
        EXPECT_TRUE(started.Succeeded()) << started.message;
        if (!started.Succeeded()) return Json::object();
        bool completed = false;
        EXPECT_TRUE(client.Wait(10000, completed).Succeeded());
        EXPECT_TRUE(completed);
        std::string text;
        const auto serialized = client.SerializeResult(text);
        EXPECT_TRUE(serialized.Succeeded()) << serialized.message;
        return text.empty() ? Json::object() : Json::parse(text);
    }
};
} // namespace

TEST_F(ReferenceEngineTests, AllMigratedPackagesPreserveCanonicalAndAliasExecution)
{
    for (const bool aliases : {false, true})
    {
        const auto result = Run(Plan(aliases));
        ASSERT_EQ(result.value("status", ""), "passed") << result.dump();
        EXPECT_EQ(result["summary"]["plannedSteps"], 4);
        EXPECT_EQ(result["summary"]["passedSteps"], 4);
        for (const auto message : {"Power On completed.", "Send CAN Message completed.",
                                   "Power Off completed.", "Native command invoked the simulated driver successfully."})
            EXPECT_NE(result.dump().find(message), std::string::npos);
    }
    std::string snapshot;
    ASSERT_TRUE(client.GetCatalogSnapshot(snapshot).Succeeded());
    const auto catalog = Json::parse(snapshot);
    EXPECT_EQ(catalog["status"], "active");
    ASSERT_EQ(catalog["extensions"].size(), 4U);
    std::set<std::string> ids;
    for (const auto &extension : catalog["extensions"])
        for (const auto &component : extension["components"])
            ids.insert(component["typeId"].get<std::string>());
    EXPECT_EQ(ids, (std::set<std::string>{
        "com.artest.command.power.turn-on", "com.artest.command.power.turn-off",
        "com.artest.command.can.send", "com.artest.command.sample.power-cycle",
        "com.artest.driver.sim.can", "com.artest.driver.sim.power", "com.artest.driver.sim.power-legacy"}));
}

TEST_F(ReferenceEngineTests, CanInitializationAndCleanupFailuresRemainVisible)
{
    auto plan = Plan(false);
    plan["instruments"][1]["config"] = {{"failInitialize", true}, {"failShutdown", true}};
    const auto result = Run(plan);
    ASSERT_EQ(result.value("status", ""), "error") << result.dump();
    EXPECT_EQ(result["summary"]["executedSteps"], 0);
    EXPECT_NE(result.dump().find("CAN_RESOURCE_MISSING"), std::string::npos);
    EXPECT_NE(result.dump().find("Simulated CAN cleanup failure."), std::string::npos);
}

TEST_F(ReferenceEngineTests, InvalidCanFrameDoesNotReportSuccessfulExecution)
{
    auto plan = Plan(false);
    plan["commands"] = Json::array({plan["commands"][1]});
    plan["commands"][0]["params"]["dlc"] = 1;
    // The schema validates individual fields; the driver validates their relationship.
    const auto result = Run(plan);
    EXPECT_NE(result.value("status", ""), "passed");
    EXPECT_EQ(result["summary"]["passedSteps"], 0);
    EXPECT_EQ(result["summary"]["executedSteps"], 1);
    EXPECT_NE(result.dump().find("Invalid CAN identifier, DLC or data length."), std::string::npos);
}

TEST_F(ReferenceEngineTests, TimeoutAndPowerCleanupFailureCannotBecomePassed)
{
    auto plan = Plan(false);
    plan["instruments"][0]["config"]["failShutdown"] = true;
    plan["commands"] = Json::array({plan["commands"][3]});
    plan["commands"][0]["params"]["holdMs"] = 5000;
    plan["commands"][0]["policy"] = {{"timeoutMs", 20}, {"maxAttempts", 1}, {"onFailure", "stop"}};
    const auto result = Run(plan);
    EXPECT_NE(result.value("status", ""), "passed");
    EXPECT_EQ(result["summary"]["timedOutSteps"], 1);
    ASSERT_EQ(result["steps"].size(), 1U);
    // A post-hoc timeout verdict is insufficient: the DLL must observe the deadline.
    // This generous bound separates a cooperative 20 ms wait from the full 5 s delay.
    EXPECT_LT(result["steps"][0]["durationMs"].get<int>(), 2500);
    EXPECT_NE(result.dump().find("Simulated shutdown failure was requested."), std::string::npos);
}

TEST(ReferenceMetadataTests, GeneratedPackagesPreserveThePreMigrationContracts)
{
    const auto root = Packages().parent_path().parent_path().parent_path().parent_path();
    std::ifstream baselineFile{root / "tests/TestSupport/Fixtures/reference-metadata-before-d3-4-3.json"};
    ASSERT_TRUE(baselineFile.is_open());
    const auto baseline = Json::parse(baselineFile);
    const auto normalize = [](Json manifest) {
        // Only packaging representation and editorial text may change.
        // Compare components by identity, not their registration/serialization order.
        manifest.erase("schemaVersion");
        manifest.erase("integrity");
        manifest.erase("description");
        Json components = Json::object();
        for (auto component : manifest["components"])
        {
            component.erase("description");
            for (auto &schema : component["schemas"]) schema.erase("path");
            const auto id = component["typeId"].get<std::string>();
            components[id] = std::move(component);
        }
        manifest["components"] = std::move(components);
        return manifest;
    };
    for (const auto name : {"ARTestCmdHardware", "ARTestCmdSample", "ARTestDrvSimCAN", "ARTestDrvSimPower"})
    {
        SCOPED_TRACE(name);
        const auto package = Packages() / name;
        EXPECT_FALSE(std::filesystem::exists(root / "source" / name / "artest-extension.json"));
        EXPECT_TRUE(std::filesystem::exists(package / ".artest-generated-package.json"));
        std::ifstream manifestFile{package / "artest-extension.json"};
        ASSERT_TRUE(manifestFile.is_open());
        const auto actual = Json::parse(manifestFile);
        EXPECT_EQ(actual["schemaVersion"], 2);
        EXPECT_EQ(normalize(actual), normalize(baseline.at(name).at("manifest")));
        for (const auto &component : actual["components"])
            for (const auto &reference : component["schemas"])
            {
                std::ifstream schemaFile{package / reference["path"].get<std::string>()};
                ASSERT_TRUE(schemaFile.is_open());
                auto schema = Json::parse(schemaFile);
                auto expected = baseline.at(name).at("schemas").at(reference["schemaId"].get<std::string>());
                // Empty required and omitted required both mean no required properties.
                if (expected.value("required", Json::array()).empty()) expected.erase("required");
                if (schema.value("required", Json::array()).empty()) schema.erase("required");
                EXPECT_EQ(schema, expected) << reference["schemaId"];
            }
    }
}

#include "../source/ARTestCLI/CliApplication.h"
#include "../source/ThirdParty/json.hpp"

#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct CliResult
    {
        int exitCode = -1;
        std::string output;
        std::string error;
    };

    [[nodiscard]] std::filesystem::path RepositoryRoot()
    {
        std::wstring buffer(32768, L'\0');
        const auto size = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        buffer.resize(size);
        auto directory = std::filesystem::path{buffer}.parent_path();
        for (int index = 0; index < 4; ++index)
            directory = directory.parent_path();
        return directory;
    }

    [[nodiscard]] CliResult Invoke(
        std::vector<std::string> arguments,
        std::string inputText = {})
    {
        std::istringstream input{std::move(inputText)};
        std::ostringstream output;
        std::ostringstream error;
        artest::cli::CliApplication application{input, output, error};
        const auto exitCode = application.Run(arguments);
        return {exitCode, output.str(), error.str()};
    }

    [[nodiscard]] std::string PathText(
        const std::filesystem::path& relativePath)
    {
        return (RepositoryRoot() / relativePath).string();
    }
}

TEST(CliThinHostTests, CompilePreservesTheLegacySuccessContract)
{
    const auto result = Invoke({
        "compile", PathText("source/Scripts/TestScript.json")});

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.output, "Valid script. No instruments were initialized.\n");
    EXPECT_TRUE(result.error.empty());
}

TEST(CliThinHostTests, RunPreservesEventsSummaryAndExitCode)
{
    const auto result = Invoke({
        "run", PathText("source/Scripts/TestScript.json")});

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("[State] INITIALIZING"), std::string::npos);
    EXPECT_NE(result.output.find("|> Executing step 1: PowerSupply.TurnOn"), std::string::npos);
    EXPECT_NE(result.output.find("[State] CLEANING_UP"), std::string::npos);
    EXPECT_NE(result.output.find("[State] COMPLETED"), std::string::npos);
    EXPECT_NE(result.output.find("Execution finished with PASSED"), std::string::npos);
    EXPECT_TRUE(result.error.empty());
}

TEST(CliThinHostTests, DebugPausesAtTheFirstStepThroughThePublicCallback)
{
    const auto result = Invoke(
        {"debug", PathText("source/Scripts/TestScript.json")},
        "c\n");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(
        result.output.find("[Debug] Paused at step 1: PowerSupply.TurnOn"),
        std::string::npos);
    EXPECT_NE(result.output.find("[State] COMPLETED"), std::string::npos);
}

TEST(CliThinHostTests, BreakPausesOnlyAtTheRequestedZeroBasedIndex)
{
    const auto result = Invoke(
        {"break", PathText("source/Scripts/TestScript.json"), "1"},
        "c\n");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(
        result.output.find("[Debug] Paused at step 1"),
        std::string::npos);
    EXPECT_NE(
        result.output.find("[Debug] Paused at step 2: Time.WaitMs"),
        std::string::npos);
}

TEST(CliThinHostTests, InvalidScriptKeepsExitCodeThreeAndStructuredDiagnostic)
{
    const auto result = Invoke({
        "compile",
        PathText("quality/manual-tests/stage-a/data/unknown-command.json")});

    EXPECT_EQ(result.exitCode, 3);
    EXPECT_NE(result.error.find("[COMMAND_TYPE_UNKNOWN]"), std::string::npos);
    EXPECT_TRUE(result.output.empty());
}

TEST(CliThinHostTests, MalformedJsonReportsTheOriginalScriptPath)
{
    const auto scriptPath = PathText(
        "quality/manual-tests/stage-a/data/malformed-json.json");
    const auto result = Invoke({"compile", scriptPath});

    EXPECT_EQ(result.exitCode, 3);
    EXPECT_NE(result.error.find("[SCRIPT_JSON_INVALID]"), std::string::npos);
    EXPECT_NE(result.error.find(scriptPath), std::string::npos);
    EXPECT_EQ(result.error.find("engine-api"), std::string::npos);
}

TEST(CliThinHostTests, DebugQuitCancelsThroughTheEngineAndStillCleansUp)
{
    const auto result = Invoke(
        {"debug", PathText("source/Scripts/TestScript.json")},
        "q\n");

    EXPECT_EQ(result.exitCode, 5);
    EXPECT_NE(result.output.find("[State] CLEANING_UP"), std::string::npos);
    EXPECT_NE(result.output.find("[State] CANCELLED"), std::string::npos);
    EXPECT_NE(result.output.find("Execution finished with CANCELLED"), std::string::npos);
}

TEST(CliThinHostTests, InitializationFailureKeepsExitCodeFourAndCleansUp)
{
    const auto result = Invoke({
        "run",
        PathText("quality/manual-tests/stage-a/data/initialization-failure.json")});

    EXPECT_EQ(result.exitCode, 4);
    EXPECT_NE(result.error.find("[POWER_SUPPLY_RESOURCE_MISSING]"), std::string::npos);
    EXPECT_NE(result.output.find("[State] CLEANING_UP"), std::string::npos);
    EXPECT_NE(result.output.find("[State] FAILED"), std::string::npos);
}

TEST(CliCatalogTests, ValidateEmitsStructuredReportWithoutLoadingExtensions)
{
    const auto result = Invoke({
        "extensions", "validate",
        PathText("artifacts/extensions/x64/Debug")});

    EXPECT_EQ(result.exitCode, 0) << result.error;
    EXPECT_TRUE(result.error.empty());
    const auto report = nlohmann::json::parse(result.output);
    EXPECT_EQ(report["schema"], "artest.schema.extension-catalog.v2");
    EXPECT_EQ(report["status"], "validated");
    EXPECT_TRUE(report["valid"].get<bool>());
    EXPECT_TRUE(report["extensions"].empty());
    ASSERT_EQ(report["packages"].size(), 2U);
    EXPECT_EQ(report["packages"][0]["integrity"], "verified");
    EXPECT_EQ(report["packages"][1]["integrity"], "verified");
}

TEST(CliCatalogTests, ListShowsDeterministicPackageSummary)
{
    const auto result = Invoke({
        "extensions", "list",
        PathText("artifacts/extensions/x64/Debug")});

    EXPECT_EQ(result.exitCode, 0) << result.error;
    EXPECT_NE(result.output.find("com.artest.extension.sample-command"),
        std::string::npos);
    EXPECT_NE(result.output.find("com.artest.extension.sim-power"),
        std::string::npos);
    EXPECT_NE(result.output.find("2 package(s), catalog valid."),
        std::string::npos);
}

TEST(CliCatalogTests, DoctorLoadsDescriptorsAndReportsActiveGeneration)
{
    const auto result = Invoke({
        "extensions", "doctor",
        PathText("artifacts/extensions/x64/Debug")});

    EXPECT_EQ(result.exitCode, 0) << result.error;
    EXPECT_TRUE(result.error.empty());
    EXPECT_NE(result.output.find("catalog validated and activated atomically"),
        std::string::npos);
    EXPECT_NE(result.output.find("\"status\": \"active\""),
        std::string::npos);
    EXPECT_NE(result.output.find("\"generation\": 1"),
        std::string::npos);
}

TEST(CliCatalogTests, InvalidCatalogUsesDedicatedExitCode)
{
    const auto result = Invoke({
        "extensions", "validate",
        PathText("quality/manual-tests/stage-d1/data/incompatible")});

    EXPECT_EQ(result.exitCode, 6);
    EXPECT_TRUE(result.error.empty());
    const auto report = nlohmann::json::parse(result.output);
    EXPECT_FALSE(report["valid"].get<bool>());
    EXPECT_NE(result.output.find("EXTENSION_RUNTIME_INCOMPATIBLE"),
        std::string::npos);
}

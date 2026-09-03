#include "../source/ARTest.SDK/include/ARTestEngineApi.h"
#include "../source/ThirdParty/json.hpp"

#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    struct Error
    {
        char text[2048]{};
        ARTestErrorBuffer value{
            sizeof(ARTestErrorBuffer), 0U, text, sizeof(text), 0U};
    };

    [[nodiscard]] ARTestStringView View(const std::string& value)
    {
        return {value.data(), value.size()};
    }

    [[nodiscard]] ARTestPayloadView Payload(const std::string& value)
    {
        static const std::string schema = "artest.schema.test-plan.v1";
        static const std::string media = "application/json; charset=utf-8";
        return {
            sizeof(ARTestPayloadView),
            ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema),
            View(media),
            {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}};
    }

    ARTestStatus ARTEST_ABI_CALL WriteString(
        void* context,
        const ARTestPayloadView* payload,
        ARTestErrorBuffer*) noexcept
    {
        if (context == nullptr || payload == nullptr)
            return ARTEST_STATUS_INVALID_ARGUMENT;
        try
        {
            static_cast<std::string*>(context)->assign(
                reinterpret_cast<const char*>(payload->bytes.data),
                payload->bytes.size);
            return ARTEST_STATUS_OK;
        }
        catch (...)
        {
            return ARTEST_STATUS_HOST_FAILURE;
        }
    }

    [[nodiscard]] std::string BuiltInScript()
    {
        return nlohmann::json{
            {"format", "ARTest.Script"},
            {"version", 1},
            {"instruments", nlohmann::json::array()},
            {"commands", nlohmann::json::array({
                {
                    {"stepId", 10},
                    {"name", "Time.WaitMs"},
                    {"instrument", "NoInstrument"},
                    {"params", {{"milliseconds", 0}}}
                },
                {
                    {"stepId", 20},
                    {"name", "Time.WaitMs"},
                    {"instrument", "NoInstrument"},
                    {"params", {{"milliseconds", 0}}}
                }})}}.dump();
    }

    struct ThinHostEngineFixture : ::testing::Test
    {
        void SetUp() override
        {
            api.struct_size = sizeof(api);
            Error error;
            ASSERT_EQ(
                ARTestEngine_QueryApi(
                    ARTEST_ENGINE_API_MAJOR,
                    ARTEST_ENGINE_API_MINOR,
                    &api,
                    &error.value),
                ARTEST_STATUS_OK) << error.text;
            const auto configurationText = std::string{"{}"};
            const auto configuration = Payload(configurationText);
            ASSERT_EQ(
                api.create_engine(&configuration, &engine, &error.value),
                ARTEST_STATUS_OK) << error.text;
        }

        void TearDown() override
        {
            if (engine != nullptr) api.destroy_engine(engine);
        }

        ARTestEngineApiV0 api{};
        ARTestEngineHandle engine = nullptr;
    };

    struct ControlCapture
    {
        std::vector<std::uint64_t> commandIndices;
        std::vector<std::uint64_t> stepIds;
    };

    ARTestStatus ARTEST_ABI_CALL ContinueBeforeStep(
        void* context,
        const ARTestStepExecutionInfoV0* info,
        ARTestExecutionDecision* decision,
        ARTestErrorBuffer*) noexcept
    {
        if (context == nullptr || info == nullptr || decision == nullptr)
            return ARTEST_STATUS_INVALID_ARGUMENT;
        auto& capture = *static_cast<ControlCapture*>(context);
        capture.commandIndices.push_back(info->command_index);
        capture.stepIds.push_back(info->step_id);
        *decision = ARTEST_EXECUTION_CONTINUE;
        return ARTEST_STATUS_OK;
    }

    [[nodiscard]] std::filesystem::path ExecutableDirectory()
    {
        std::wstring buffer(32768, L'\0');
        const auto size = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        buffer.resize(size);
        return std::filesystem::path{buffer}.parent_path();
    }

    [[nodiscard]] std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{input}, {}};
    }
}

TEST(StageDEngineApiCompatibilityTests, NegotiatesMinorOneWithoutWritingPastItsTable)
{
    alignas(ARTestEngineApiV0)
        std::array<unsigned char, sizeof(ARTestEngineApiV0)> storage{};
    storage.fill(0xA5U);
    auto* api = reinterpret_cast<ARTestEngineApiV0*>(storage.data());
    api->struct_size = ARTEST_ENGINE_API_V0_1_SIZE;
    Error error;

    ASSERT_EQ(
        ARTestEngine_QueryApi(0U, 1U, api, &error.value),
        ARTEST_STATUS_OK) << error.text;
    EXPECT_EQ(api->struct_size, ARTEST_ENGINE_API_V0_1_SIZE);
    EXPECT_EQ(api->api_minor, 1U);
    EXPECT_NE(api->start_session, nullptr);
    EXPECT_TRUE(std::all_of(
        storage.begin() + ARTEST_ENGINE_API_V0_1_SIZE,
        storage.end(),
        [](unsigned char value) { return value == 0xA5U; }));
}

TEST(StageDEngineApiCompatibilityTests, NegotiatesMinorTwoWithoutWritingPastItsTable)
{
    alignas(ARTestEngineApiV0)
        std::array<unsigned char, sizeof(ARTestEngineApiV0)> storage{};
    storage.fill(0xA5U);
    auto* api = reinterpret_cast<ARTestEngineApiV0*>(storage.data());
    api->struct_size = ARTEST_ENGINE_API_V0_2_SIZE;
    Error error;

    ASSERT_EQ(
        ARTestEngine_QueryApi(0U, 2U, api, &error.value),
        ARTEST_STATUS_OK) << error.text;
    EXPECT_EQ(api->struct_size, ARTEST_ENGINE_API_V0_2_SIZE);
    EXPECT_EQ(api->api_minor, 2U);
    EXPECT_NE(api->compile_plan_detailed, nullptr);
    EXPECT_NE(api->start_session_controlled, nullptr);
    EXPECT_TRUE(std::all_of(
        storage.begin() + ARTEST_ENGINE_API_V0_2_SIZE,
        storage.end(),
        [](unsigned char value) { return value == 0xA5U; }));
}

TEST_F(ThinHostEngineFixture, DetailedCompilationReturnsAllValidationDiagnostics)
{
    const auto scriptText = std::string{R"({"format":"wrong"})"};
    const auto script = Payload(scriptText);
    ARTestCompiledPlanHandle plan = reinterpret_cast<ARTestCompiledPlanHandle>(1U);
    std::string reportText;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &reportText, &WriteString};
    Error error;

    ASSERT_EQ(
        api.compile_plan_detailed(
            engine, &script, &plan, &sink, &error.value),
        ARTEST_STATUS_OK) << error.text;
    EXPECT_EQ(plan, nullptr);
    const auto report = nlohmann::json::parse(reportText);
    EXPECT_EQ(report["schema"], "artest.schema.compile-result.v1");
    EXPECT_FALSE(report["valid"].get<bool>());
    EXPECT_EQ(report["diagnostics"].size(), 4U);
}

TEST_F(ThinHostEngineFixture, ControlledSessionReceivesEveryStepBeforeExecution)
{
    const auto scriptText = BuiltInScript();
    const auto script = Payload(scriptText);
    ARTestCompiledPlanHandle plan = nullptr;
    std::string compileReport;
    ARTestResultSinkV0 compileSink{
        sizeof(ARTestResultSinkV0), 0U, &compileReport, &WriteString};
    Error error;
    ASSERT_EQ(
        api.compile_plan_detailed(
            engine, &script, &plan, &compileSink, &error.value),
        ARTEST_STATUS_OK) << error.text;
    ASSERT_NE(plan, nullptr);
    EXPECT_TRUE(nlohmann::json::parse(compileReport)["valid"].get<bool>());

    ControlCapture capture;
    const ARTestSessionOptionsV0 options{
        sizeof(ARTestSessionOptionsV0), 0U,
        &ContinueBeforeStep, &capture};
    ARTestSessionHandle session = nullptr;
    ASSERT_EQ(
        api.start_session_controlled(
            engine, plan, &options, &session, &error.value),
        ARTEST_STATUS_OK) << error.text;

    ARTestBool32 completed = ARTEST_FALSE;
    ASSERT_EQ(
        api.wait_session(session, UINT32_MAX, &completed, &error.value),
        ARTEST_STATUS_OK) << error.text;
    EXPECT_EQ(completed, ARTEST_TRUE);
    EXPECT_EQ(capture.commandIndices, (std::vector<std::uint64_t>{0U, 1U}));
    EXPECT_EQ(capture.stepIds, (std::vector<std::uint64_t>{10U, 20U}));

    ARTestResultHandle result = nullptr;
    ASSERT_EQ(
        api.get_session_result(session, &result, &error.value),
        ARTEST_STATUS_OK) << error.text;
    std::string resultText;
    ARTestResultSinkV0 resultSink{
        sizeof(ARTestResultSinkV0), 0U, &resultText, &WriteString};
    ASSERT_EQ(
        api.serialize_result(result, &resultSink, &error.value),
        ARTEST_STATUS_OK) << error.text;
    EXPECT_EQ(nlohmann::json::parse(resultText)["status"], "passed");

    api.destroy_result(result);
    api.destroy_session(session);
    api.destroy_compiled_plan(plan);
}

TEST(StageDThinHostArchitectureTests, CliDependsOnlyOnThePublicSdkAndEngineDll)
{
    auto repository = ExecutableDirectory();
    for (int index = 0; index < 4; ++index) repository = repository.parent_path();
    const auto cliDirectory = repository / "source" / "ARTestCLI";
    ASSERT_TRUE(std::filesystem::is_directory(cliDirectory)) << cliDirectory;

    for (const auto& entry : std::filesystem::directory_iterator{cliDirectory})
    {
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension().string();
        if (extension != ".cpp" && extension != ".h"
            && extension != ".hpp" && extension != ".vcxproj")
        {
            continue;
        }
        EXPECT_EQ(ReadText(entry.path()).find("ARTestEngine.Core"), std::string::npos) << entry.path();
    }

    const auto project = ReadText(cliDirectory / "ARTestCLI.vcxproj");
    EXPECT_NE(project.find("ARTestEngine\\ARTestEngine.vcxproj"), std::string::npos);
    EXPECT_NE(project.find("ARTest.SDK\\include"), std::string::npos);
}

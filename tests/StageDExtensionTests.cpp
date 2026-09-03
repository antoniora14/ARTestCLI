#include "../source/ARTest.SDK/include/ARTestEngineApi.h"
#include "../source/ThirdParty/json.hpp"

#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct Error
    {
        char text[2048]{};
        ARTestErrorBuffer value{sizeof(ARTestErrorBuffer), 0U, text, sizeof(text), 0U};
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
            sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema), View(media),
            {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}};
    }

    [[nodiscard]] std::filesystem::path ExecutableDirectory()
    {
        std::wstring buffer(32768, L'\0');
        const auto size = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        buffer.resize(size);
        return std::filesystem::path{buffer}.parent_path();
    }

    [[nodiscard]] std::filesystem::path ExtensionRoot()
    {
        const auto binary = ExecutableDirectory();
        return binary.parent_path().parent_path().parent_path()
            / "extensions" / binary.parent_path().filename() / binary.filename();
    }

    [[nodiscard]] std::string Script(int holdMs, bool failShutdown = false)
    {
        return nlohmann::json{
            {"format", "ARTest.Script"},
            {"version", 1},
            {"instruments", nlohmann::json::array({
                {
                    {"type", "com.artest.driver.sim.power"},
                    {"id", "SimPower1"},
                    {"config", {{"failShutdown", failShutdown}}}
                }})},
            {"commands", nlohmann::json::array({
                {
                    {"stepId", 1},
                    {"name", "com.artest.command.sample.power-cycle"},
                    {"instrument", "SimPower1"},
                    {"params", {
                        {"channel", 1},
                        {"voltage", 12.0},
                        {"holdMs", holdMs}}},
                    {"policy", {
                        {"maxAttempts", 1},
                        {"timeoutMs", holdMs + 2000},
                        {"onFailure", "stop"}}}
                }})}}.dump();
    }

    class TemporaryCatalog final
    {
    public:
        explicit TemporaryCatalog(std::string name)
            : root(std::filesystem::temp_directory_path()
                / ("ARTest-D3-" + std::move(name) + "-"
                    + std::to_string(GetCurrentProcessId()) + "-"
                    + std::to_string(GetTickCount64())))
        {
            std::filesystem::create_directories(root);
        }

        ~TemporaryCatalog()
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        TemporaryCatalog(const TemporaryCatalog&) = delete;
        TemporaryCatalog& operator=(const TemporaryCatalog&) = delete;

        [[nodiscard]] std::filesystem::path CopyPackage(
            const std::string& sourceName,
            const std::string& destinationName) const
        {
            const auto destination = root / destinationName;
            std::filesystem::copy(
                ExtensionRoot() / sourceName,
                destination,
                std::filesystem::copy_options::recursive);
            return destination;
        }

        [[nodiscard]] nlohmann::json ReadManifest(
            const std::filesystem::path& package) const
        {
            std::ifstream input{package / "artest-extension.json"};
            nlohmann::json value;
            input >> value;
            return value;
        }

        void WriteManifest(
            const std::filesystem::path& package,
            const nlohmann::json& value) const
        {
            std::ofstream output{package / "artest-extension.json",
                std::ios::binary | std::ios::trunc};
            output << value.dump(2);
        }

        std::filesystem::path root;
    };

    struct Capture
    {
        std::mutex mutex;
        std::vector<std::string> values;
    };

    ARTestStatus ARTEST_ABI_CALL WriteCapture(
        void* context, const ARTestPayloadView* payload, ARTestErrorBuffer*) noexcept
    {
        if (context == nullptr || payload == nullptr) return ARTEST_STATUS_INVALID_ARGUMENT;
        auto& capture = *static_cast<Capture*>(context);
        std::scoped_lock lock{capture.mutex};
        capture.values.emplace_back(
            reinterpret_cast<const char*>(payload->bytes.data), payload->bytes.size);
        return ARTEST_STATUS_OK;
    }

    void ARTEST_ABI_CALL CaptureEvent(
        void* context, const ARTestPayloadView* payload) noexcept
    {
        static_cast<void>(WriteCapture(context, payload, nullptr));
    }

    class EngineFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_api.struct_size = sizeof(m_api);
            Error error;
            ASSERT_EQ(
                ARTestEngine_QueryApi(
                    ARTEST_ENGINE_API_MAJOR, ARTEST_ENGINE_API_MINOR,
                    &m_api, &error.value),
                ARTEST_STATUS_OK) << error.text;
            const auto configurationText = std::string{"{}"};
            const auto configuration = Payload(configurationText);
            ASSERT_EQ(
                m_api.create_engine(&configuration, &m_engine, &error.value),
                ARTEST_STATUS_OK) << error.text;
        }

        void TearDown() override
        {
            if (m_engine != nullptr) m_api.destroy_engine(m_engine);
        }

        void LoadExtensions()
        {
            Error error;
            const auto root = ExtensionRoot().string();
            ASSERT_EQ(
                m_api.refresh_catalog(m_engine, View(root), &error.value),
                ARTEST_STATUS_OK) << root << ": " << error.text;
        }

        ARTestEngineApiV0 m_api{};
        ARTestEngineHandle m_engine = nullptr;
    };
}


TEST_F(EngineFixture, OneSessionOwnsTheEngineUntilItsHandleIsDestroyed)
{
    const auto text = Script(0);
    const auto payload = Payload(text);
    Error error;
    ARTestCompiledPlanHandle plan = nullptr;
    ASSERT_EQ(m_api.compile_plan(m_engine, &payload, &plan, &error.value), ARTEST_STATUS_OK) << error.text;
    auto planOwner = std::unique_ptr<ARTestCompiledPlanOpaque, ARTestDestroyCompiledPlanFn>(plan, m_api.destroy_compiled_plan);
    ARTestSessionHandle first = nullptr;
    ASSERT_EQ(m_api.start_session(m_engine, plan, &first, &error.value), ARTEST_STATUS_OK);
    auto firstOwner = std::unique_ptr<ARTestSessionOpaque, ARTestDestroySessionFn>(first, m_api.destroy_session);
    ARTestSessionHandle second = nullptr;
    EXPECT_EQ(m_api.start_session(m_engine, plan, &second, &error.value), ARTEST_STATUS_INVALID_STATE);
    ARTestBool32 completed = ARTEST_FALSE;
    ASSERT_EQ(m_api.wait_session(first, 10000, &completed, &error.value), ARTEST_STATUS_OK);
    ASSERT_EQ(completed, ARTEST_TRUE);
    EXPECT_EQ(m_api.start_session(m_engine, plan, &second, &error.value), ARTEST_STATUS_INVALID_STATE);
    firstOwner.reset();
    ASSERT_EQ(m_api.start_session(m_engine, plan, &second, &error.value), ARTEST_STATUS_OK) << error.text;
    auto secondOwner = std::unique_ptr<ARTestSessionOpaque, ARTestDestroySessionFn>(second, m_api.destroy_session);
    EXPECT_EQ(m_api.wait_session(second, 10000, &completed, &error.value), ARTEST_STATUS_OK);
    EXPECT_EQ(completed, ARTEST_TRUE);
}
TEST(StageDEngineApiTests, NegotiatesExactExperimentalVersion)
{
    ARTestEngineApiV0 api{};
    api.struct_size = sizeof(api);
    Error error;
    EXPECT_EQ(
        ARTestEngine_QueryApi(
            ARTEST_ENGINE_API_MAJOR, ARTEST_ENGINE_API_MINOR, &api, &error.value),
        ARTEST_STATUS_OK);
    EXPECT_EQ(api.api_major, ARTEST_ENGINE_API_MAJOR);
    EXPECT_EQ(api.api_minor, ARTEST_ENGINE_API_MINOR);

    api = {};
    api.struct_size = sizeof(api);
    EXPECT_EQ(
        ARTestEngine_QueryApi(1U, 0U, &api, &error.value),
        ARTEST_STATUS_INCOMPATIBLE_ABI);
}

TEST_F(EngineFixture, LoadsCatalogAndExecutesCommandThroughDriverService)
{
    LoadExtensions();
    Capture events;
    ARTestSubscriptionHandle subscription = nullptr;
    Error error;
    ASSERT_EQ(
        m_api.subscribe_events(
            m_engine, &CaptureEvent, &events, &subscription, &error.value),
        ARTEST_STATUS_OK) << error.text;

    const auto scriptText = Script(75);
    const auto script = Payload(scriptText);
    ARTestCompiledPlanHandle plan = nullptr;
    ASSERT_EQ(
        m_api.compile_plan(m_engine, &script, &plan, &error.value),
        ARTEST_STATUS_OK) << error.text;
    ARTestSessionHandle session = nullptr;
    ASSERT_EQ(
        m_api.start_session(m_engine, plan, &session, &error.value),
        ARTEST_STATUS_OK) << error.text;

    ARTestBool32 completed = ARTEST_FALSE;
    ASSERT_EQ(
        m_api.wait_session(session, UINT32_MAX, &completed, &error.value),
        ARTEST_STATUS_OK) << error.text;
    ASSERT_EQ(completed, ARTEST_TRUE);

    ARTestResultHandle result = nullptr;
    ASSERT_EQ(
        m_api.get_session_result(session, &result, &error.value),
        ARTEST_STATUS_OK) << error.text;
    Capture serialized;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &serialized, &WriteCapture};
    ASSERT_EQ(
        m_api.serialize_result(result, &sink, &error.value),
        ARTEST_STATUS_OK) << error.text;
    ASSERT_EQ(serialized.values.size(), 1U);
    const auto report = nlohmann::json::parse(serialized.values.front());
    EXPECT_EQ(report["status"], "passed");
    EXPECT_EQ(report["summary"]["passedSteps"], 1);

    {
        std::scoped_lock lock{events.mutex};
        const auto joined = std::accumulate(
            events.values.begin(), events.values.end(), std::string{});
        EXPECT_NE(joined.find("completed through driver service"), std::string::npos);
        EXPECT_NE(joined.find("shut down"), std::string::npos);
        std::vector<std::string> states;
        for (const auto& eventText : events.values)
        {
            const auto event = nlohmann::json::parse(eventText);
            if (event["kind"] == 5)
                states.push_back(event["message"].get<std::string>());
        }
        const std::vector<std::string> expected{
            "INITIALIZING", "RUNNING", "CLEANING_UP", "COMPLETED"};
        EXPECT_EQ(states, expected);
    }
    m_api.destroy_result(result);
    m_api.destroy_session(session);
    m_api.unsubscribe_events(m_engine, subscription);

    std::size_t eventCountAfterUnsubscribe = 0U;
    {
        std::scoped_lock lock{events.mutex};
        eventCountAfterUnsubscribe = events.values.size();
    }
    ARTestSessionHandle secondSession = nullptr;
    ASSERT_EQ(
        m_api.start_session(m_engine, plan, &secondSession, &error.value),
        ARTEST_STATUS_OK);
    completed = ARTEST_FALSE;
    ASSERT_EQ(
        m_api.wait_session(secondSession, UINT32_MAX, &completed, &error.value),
        ARTEST_STATUS_OK);
    {
        std::scoped_lock lock{events.mutex};
        EXPECT_EQ(events.values.size(), eventCountAfterUnsubscribe);
    }
    m_api.destroy_session(secondSession);
    m_api.destroy_compiled_plan(plan);
}

TEST_F(EngineFixture, CancellationStillShutsDownTheDriver)
{
    LoadExtensions();
    Capture events;
    ARTestSubscriptionHandle subscription = nullptr;
    Error error;
    ASSERT_EQ(
        m_api.subscribe_events(
            m_engine, &CaptureEvent, &events, &subscription, &error.value),
        ARTEST_STATUS_OK);
    const auto scriptText = Script(1500);
    const auto script = Payload(scriptText);
    ARTestCompiledPlanHandle plan = nullptr;
    ASSERT_EQ(m_api.compile_plan(m_engine, &script, &plan, &error.value), ARTEST_STATUS_OK);
    ARTestSessionHandle session = nullptr;
    ASSERT_EQ(m_api.start_session(m_engine, plan, &session, &error.value), ARTEST_STATUS_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    ASSERT_EQ(m_api.cancel_session(session, &error.value), ARTEST_STATUS_OK);
    ARTestBool32 completed = ARTEST_FALSE;
    ASSERT_EQ(
        m_api.wait_session(session, UINT32_MAX, &completed, &error.value),
        ARTEST_STATUS_OK);
    ARTestResultHandle result = nullptr;
    ASSERT_EQ(m_api.get_session_result(session, &result, &error.value), ARTEST_STATUS_OK);
    Capture serialized;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &serialized, &WriteCapture};
    ASSERT_EQ(m_api.serialize_result(result, &sink, &error.value), ARTEST_STATUS_OK);
    EXPECT_EQ(nlohmann::json::parse(serialized.values.front())["status"], "cancelled");
    {
        std::scoped_lock lock{events.mutex};
        const auto joined = std::accumulate(
            events.values.begin(), events.values.end(), std::string{});
        EXPECT_NE(joined.find("shut down"), std::string::npos);
    }
    m_api.destroy_result(result);
    m_api.destroy_session(session);
    m_api.destroy_compiled_plan(plan);
    m_api.unsubscribe_events(m_engine, subscription);
}

TEST_F(EngineFixture, HostWaitTimeoutDoesNotCancelTheSession)
{
    LoadExtensions();
    Error error;
    const auto scriptText = Script(250);
    const auto script = Payload(scriptText);
    ARTestCompiledPlanHandle plan = nullptr;
    ASSERT_EQ(m_api.compile_plan(m_engine, &script, &plan, &error.value), ARTEST_STATUS_OK);
    ARTestSessionHandle session = nullptr;
    ASSERT_EQ(m_api.start_session(m_engine, plan, &session, &error.value), ARTEST_STATUS_OK);

    ARTestBool32 completed = ARTEST_TRUE;
    ASSERT_EQ(m_api.wait_session(session, 1U, &completed, &error.value), ARTEST_STATUS_OK);
    EXPECT_EQ(completed, ARTEST_FALSE);
    ASSERT_EQ(
        m_api.wait_session(session, UINT32_MAX, &completed, &error.value),
        ARTEST_STATUS_OK);
    EXPECT_EQ(completed, ARTEST_TRUE);
    ARTestResultHandle result = nullptr;
    ASSERT_EQ(m_api.get_session_result(session, &result, &error.value), ARTEST_STATUS_OK);
    Capture serialized;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &serialized, &WriteCapture};
    ASSERT_EQ(m_api.serialize_result(result, &sink, &error.value), ARTEST_STATUS_OK);
    EXPECT_EQ(nlohmann::json::parse(serialized.values.front())["status"], "passed");
    m_api.destroy_result(result);
    m_api.destroy_session(session);
    m_api.destroy_compiled_plan(plan);
}

TEST_F(EngineFixture, CleanupFailureOverridesAnOtherwisePassingRun)
{
    LoadExtensions();
    Error error;
    const auto scriptText = Script(1, true);
    const auto script = Payload(scriptText);
    ARTestCompiledPlanHandle plan = nullptr;
    ASSERT_EQ(m_api.compile_plan(m_engine, &script, &plan, &error.value), ARTEST_STATUS_OK);
    ARTestSessionHandle session = nullptr;
    ASSERT_EQ(m_api.start_session(m_engine, plan, &session, &error.value), ARTEST_STATUS_OK);
    ARTestBool32 completed = ARTEST_FALSE;
    ASSERT_EQ(
        m_api.wait_session(session, UINT32_MAX, &completed, &error.value),
        ARTEST_STATUS_OK);
    ARTestResultHandle result = nullptr;
    ASSERT_EQ(m_api.get_session_result(session, &result, &error.value), ARTEST_STATUS_OK);
    Capture serialized;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &serialized, &WriteCapture};
    ASSERT_EQ(m_api.serialize_result(result, &sink, &error.value), ARTEST_STATUS_OK);
    const auto report = nlohmann::json::parse(serialized.values.front());
    EXPECT_EQ(report["status"], "error");
    EXPECT_EQ(report["failureKind"], 3);
    m_api.destroy_result(result);
    m_api.destroy_session(session);
    m_api.destroy_compiled_plan(plan);
}

TEST_F(EngineFixture, RejectsACompiledPlanOwnedByAnotherEngine)
{
    LoadExtensions();
    Error error;
    const auto scriptText = Script(1);
    const auto script = Payload(scriptText);
    ARTestCompiledPlanHandle plan = nullptr;
    ASSERT_EQ(m_api.compile_plan(m_engine, &script, &plan, &error.value), ARTEST_STATUS_OK);

    const auto configurationText = std::string{"{}"};
    const auto configuration = Payload(configurationText);
    ARTestEngineHandle other = nullptr;
    ASSERT_EQ(m_api.create_engine(&configuration, &other, &error.value), ARTEST_STATUS_OK);
    ARTestSessionHandle session = nullptr;
    EXPECT_EQ(
        m_api.start_session(other, plan, &session, &error.value),
        ARTEST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(session, nullptr);
    m_api.destroy_engine(other);
    m_api.destroy_compiled_plan(plan);
}

TEST_F(EngineFixture, RejectsAnIncompatibleManifestBeforeLoadingCode)
{
    const auto source = ExtensionRoot() / "ARTestDrvSimPower";
    const auto root = std::filesystem::temp_directory_path()
        / ("ARTest-D1-" + std::to_string(GetCurrentProcessId()));
    const auto package = root / "Incompatible";
    std::filesystem::create_directories(package);
    std::filesystem::copy_file(
        source / "ARTestDrvSimPower.dll",
        package / "ARTestDrvSimPower.dll",
        std::filesystem::copy_options::overwrite_existing);
    std::ifstream input{source / "artest-extension.json"};
    nlohmann::json manifest;
    input >> manifest;
    manifest["runtime"]["abi"]["major"] = 9;
    std::ofstream output{package / "artest-extension.json"};
    output << manifest.dump(2);
    output.close();

    Error error;
    const auto rootText = root.string();
    EXPECT_EQ(
        m_api.refresh_catalog(m_engine, View(rootText), &error.value),
        ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_NE(std::string{error.text}.find("EXTENSION_RUNTIME_INCOMPATIBLE"), std::string::npos);
    std::filesystem::remove_all(root);
}

TEST_F(EngineFixture, ValidatesCatalogWithoutLoadingNativeCode)
{
    TemporaryCatalog catalog{"validate-no-load"};
    const auto package = catalog.CopyPackage("ARTestDrvSimPower", "Driver");
    const auto library = package / "ARTestDrvSimPower.dll";
    Capture capture;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &capture, &WriteCapture};
    Error error;
    const auto root = catalog.root.string();

    ASSERT_EQ(m_api.validate_catalog(
        m_engine, View(root), &sink, &error.value), ARTEST_STATUS_OK) << error.text;
    ASSERT_EQ(capture.values.size(), 1U);
    const auto report = nlohmann::json::parse(capture.values.front());
    EXPECT_EQ(report["schema"], "artest.schema.extension-catalog.v2");
    EXPECT_TRUE(report["valid"].get<bool>());
    EXPECT_EQ(report["status"], "validated");

    // Deletion succeeds only because validation did not retain a LoadLibrary handle.
    EXPECT_TRUE(std::filesystem::remove(library));
}

TEST_F(EngineFixture, ReportsMalformedManifestWithoutThrowingAcrossTheAbi)
{
    TemporaryCatalog catalog{"malformed"};
    const auto package = catalog.CopyPackage("ARTestDrvSimPower", "Driver");
    {
        std::ofstream output{package / "artest-extension.json",
            std::ios::binary | std::ios::trunc};
        output << R"({"schemaVersion": 1,)";
    }
    Capture capture;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &capture, &WriteCapture};
    Error error;
    const auto root = catalog.root.string();

    ASSERT_EQ(m_api.validate_catalog(
        m_engine, View(root), &sink, &error.value), ARTEST_STATUS_OK) << error.text;
    const auto report = nlohmann::json::parse(capture.values.front());
    EXPECT_FALSE(report["valid"].get<bool>());
    EXPECT_NE(report.dump().find("EXTENSION_MANIFEST_JSON_INVALID"),
        std::string::npos);
}

TEST_F(EngineFixture, RejectsRuntimeEntryOutsideItsPackage)
{
    TemporaryCatalog catalog{"path-escape"};
    const auto package = catalog.CopyPackage("ARTestDrvSimPower", "Driver");
    std::filesystem::copy_file(
        package / "ARTestDrvSimPower.dll",
        catalog.root / "outside.dll");
    auto manifest = catalog.ReadManifest(package);
    manifest["runtime"]["entry"] = "../outside.dll";
    catalog.WriteManifest(package, manifest);

    Capture capture;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &capture, &WriteCapture};
    Error error;
    const auto root = catalog.root.string();
    ASSERT_EQ(m_api.validate_catalog(
        m_engine, View(root), &sink, &error.value), ARTEST_STATUS_OK);
    const auto report = nlohmann::json::parse(capture.values.front());
    EXPECT_FALSE(report["valid"].get<bool>());
    EXPECT_NE(report.dump().find("EXTENSION_ENTRY_INVALID"), std::string::npos);
}

TEST_F(EngineFixture, RejectsDuplicateExtensionAndComponentIdentifiers)
{
    TemporaryCatalog catalog{"duplicates"};
    static_cast<void>(catalog.CopyPackage("ARTestDrvSimPower", "DriverA"));
    static_cast<void>(catalog.CopyPackage("ARTestDrvSimPower", "DriverB"));

    Capture capture;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &capture, &WriteCapture};
    Error error;
    const auto root = catalog.root.string();
    ASSERT_EQ(m_api.validate_catalog(
        m_engine, View(root), &sink, &error.value), ARTEST_STATUS_OK);
    const auto text = capture.values.front();
    const auto report = nlohmann::json::parse(text);
    EXPECT_FALSE(report["valid"].get<bool>());
    EXPECT_NE(text.find("EXTENSION_ID_DUPLICATE"), std::string::npos);
    EXPECT_NE(text.find("EXTENSION_COMPONENT_DUPLICATE"), std::string::npos);
}

TEST_F(EngineFixture, RejectsRuntimeEntryWithMismatchedIntegrityHash)
{
    TemporaryCatalog catalog{"integrity"};
    const auto package = catalog.CopyPackage("ARTestDrvSimPower", "Driver");
    auto manifest = catalog.ReadManifest(package);
    manifest["integrity"] = {{"sha256", std::string(64U, '0')}};
    catalog.WriteManifest(package, manifest);

    Capture capture;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &capture, &WriteCapture};
    Error error;
    const auto root = catalog.root.string();
    ASSERT_EQ(m_api.validate_catalog(
        m_engine, View(root), &sink, &error.value), ARTEST_STATUS_OK);
    const auto report = nlohmann::json::parse(capture.values.front());
    EXPECT_FALSE(report["valid"].get<bool>());
    EXPECT_NE(report.dump().find("EXTENSION_INTEGRITY_MISMATCH"),
        std::string::npos);
}

TEST_F(EngineFixture, FailedActivationDoesNotPoisonTheNextValidActivation)
{
    TemporaryCatalog catalog{"failure-containment"};
    const auto package = catalog.CopyPackage("ARTestDrvSimPower", "Driver");
    const auto validManifest = catalog.ReadManifest(package);
    auto invalidManifest = validManifest;
    invalidManifest["runtime"]["abi"]["major"] = 99;
    catalog.WriteManifest(package, invalidManifest);

    Error error;
    const auto root = catalog.root.string();
    EXPECT_EQ(m_api.refresh_catalog(
        m_engine, View(root), &error.value), ARTEST_STATUS_EXTENSION_FAILURE);

    catalog.WriteManifest(package, validManifest);
    Error secondError;
    EXPECT_EQ(m_api.refresh_catalog(
        m_engine, View(root), &secondError.value), ARTEST_STATUS_OK)
        << secondError.text;

    Capture capture;
    ARTestResultSinkV0 sink{
        sizeof(ARTestResultSinkV0), 0U, &capture, &WriteCapture};
    ASSERT_EQ(m_api.get_catalog_snapshot(
        m_engine, &sink, &secondError.value), ARTEST_STATUS_OK);
    const auto snapshot = nlohmann::json::parse(capture.values.front());
    EXPECT_EQ(snapshot["status"], "active");
    EXPECT_EQ(snapshot["generation"], 1U);
    EXPECT_EQ(snapshot["extensions"].size(), 1U);
}

TEST(StageDNativeLoaderTests, ReleasesNativeModulesWhenTheEngineIsDestroyed)
{
    const auto source = ExtensionRoot() / "ARTestDrvSimPower";
    const auto root = std::filesystem::temp_directory_path()
        / ("ARTest-D1-Unload-" + std::to_string(GetCurrentProcessId()));
    const auto package = root / "Driver";
    std::filesystem::create_directories(package);
    const auto library = package / "ARTestDrvSimPower.dll";
    std::filesystem::copy_file(
        source / "ARTestDrvSimPower.dll", library,
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(
        source / "artest-extension.json", package / "artest-extension.json",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy(source / "schemas", package / "schemas",
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);

    ARTestEngineApiV0 api{};
    api.struct_size = sizeof(api);
    Error error;
    ASSERT_EQ(
        ARTestEngine_QueryApi(
            ARTEST_ENGINE_API_MAJOR, ARTEST_ENGINE_API_MINOR,
            &api, &error.value),
        ARTEST_STATUS_OK);
    const auto configurationText = std::string{"{}"};
    const auto configuration = Payload(configurationText);
    ARTestEngineHandle engine = nullptr;
    ASSERT_EQ(api.create_engine(&configuration, &engine, &error.value), ARTEST_STATUS_OK);
    const auto rootText = root.string();
    ASSERT_EQ(
        api.refresh_catalog(engine, View(rootText), &error.value),
        ARTEST_STATUS_OK) << error.text;
    api.destroy_engine(engine);

    EXPECT_TRUE(std::filesystem::remove(library));
    std::filesystem::remove_all(root);
}

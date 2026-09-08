// Explicit optional gate: scripts/test-python-runtime.ps1 enables this suite.
// Native-only builds neither install Python nor silently count these cases as passed.
#include <ARTestEngineClient.h>
#include <ARTest/Result.h>
#include <gtest/gtest.h>
#include "TestSupport/ReferenceCatalog.h"
#include <fstream>
#include <thread>
#include <atomic>
using artest::sdk::EngineClient;
using artest::sdk::Json;
namespace
{
std::filesystem::path Repository()
{
    wchar_t buffer[32768]{};
    GetModuleFileNameW(nullptr, buffer, 32768);
    auto root = std::filesystem::path{buffer}.parent_path();
    for (int i = 0; i < 4; ++i) root = root.parent_path();
    return root;
}
std::filesystem::path Binaries()
{
    wchar_t buffer[32768]{};
    GetModuleFileNameW(nullptr, buffer, 32768);
    return std::filesystem::path{buffer}.parent_path();
}
class PythonTest : public ::testing::Test
{
  protected:
    std::unique_ptr<artest::tests::ReferenceCatalog> packages;
    EngineClient client;
    std::string events;
    std::atomic_bool stepStarted{false};
    void SetUp() override
    {
        const auto root = Repository();
        const auto python = root / "artifacts/python/extensions/ARTestPySimulated";
        ASSERT_TRUE(std::filesystem::is_regular_file(python / "artest-extension.json"))
            << "Run scripts/prepare-python-example.ps1 first.";
        packages = std::make_unique<artest::tests::ReferenceCatalog>(root / "artifacts/extensions/x64" / Binaries().filename());
        std::filesystem::copy(python, packages->Root() / "ARTestPySimulated", std::filesystem::copy_options::recursive);
        const auto faults = root / "artifacts/python/test-extensions/ARTestPyFaults";
        ASSERT_TRUE(std::filesystem::is_regular_file(faults / "artest-extension.json"));
        std::filesystem::copy(faults, packages->Root() / "ARTestPyFaults", std::filesystem::copy_options::recursive);
        std::ifstream mapping(root / "artifacts/python/environments/python-environments.json");
        ASSERT_TRUE(mapping.good()) << "Prepare the example environment mapping first.";
        const Json options = {{"loadDefaultCatalog", false}, {"resultSchemaVersion", 2},
                              {"pythonEnvironments", Json::parse(mapping)}};
        auto status = client.Create(options.dump());
        ASSERT_TRUE(status.Succeeded()) << status.message;
        status = client.PrepareCatalog(packages->Root().string());
        ASSERT_TRUE(status.Succeeded()) << status.message;
        ASSERT_TRUE(client.SubscribeEvents([this](std::string_view text) {
            events += text;
            if (text.find("Starting step attempt.") != std::string_view::npos) stepStarted = true;
        }).Succeeded());
    }
    Json Plan(std::string command = "com.artest.python.command.measure-voltage",
              std::string driver = "com.artest.python.driver.power")
    {
        std::ifstream input(Repository() / "source/ARTest.Python/examples/PythonMeasurement.json");
        auto plan = Json::parse(input);
        plan["instruments"][0]["type"] = driver;
        plan["instruments"][0]["id"] = "PS1";
        plan["commands"][0]["name"] = command;
        plan["commands"][0]["instrument"] = "PS1";
        return plan;
    }
    Json Run(const Json &plan, bool cancel = false)
    {
        auto status = client.Compile(plan.dump());
        EXPECT_TRUE(status.Succeeded()) << status.message;
        if (!status.Succeeded()) return Json::object();
        status = client.Start();
        EXPECT_TRUE(status.Succeeded()) << status.message;
        if (!status.Succeeded()) return Json::object();
        if (cancel)
        {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{15};
            while (!stepStarted && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            EXPECT_TRUE(stepStarted);
            client.RequestCancel();
        }
        bool completed = false;
        status = client.Wait(30000, completed);
        EXPECT_TRUE(status.Succeeded()) << status.message;
        EXPECT_TRUE(completed);
        if (!completed) { client.RequestCancel(); return Json::object(); }
        std::string output;
        status = client.SerializeResult(output);
        EXPECT_TRUE(status.Succeeded()) << status.message;
        return Json::parse(output);
    }
};
using DISABLED_PythonIntegrationTests = PythonTest;
}
TEST_F(DISABLED_PythonIntegrationTests, PythonCommandInvokesPythonDriver)
{
    const auto result = Run(Plan());
    EXPECT_EQ(result["status"], "passed") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["outcome"]["data"]["value"], 5.0);
    EXPECT_NE(events.find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, PythonCommandInvokesNativeDriverAndPreservesMeasurement)
{
    const auto result = Run(Plan("com.artest.python.command.measure-voltage", "com.artest.driver.sim.power"));
    EXPECT_EQ(result["status"], "passed") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["outcome"]["dataSchema"], "artest.schema.measurement.voltage.v1");
}
TEST_F(DISABLED_PythonIntegrationTests, NativeCommandInvokesPythonDriver)
{
    auto plan = Plan("com.artest.command.sample.power-cycle");
    plan["commands"][0]["params"] = {{"channel", 1}, {"voltage", 12.0}, {"holdMs", 10}};
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "passed") << result.dump(2);
    EXPECT_NE(events.find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, FailedLimitIsNotATransportError)
{
    auto plan = Plan();
    plan["commands"][0]["params"]["voltage"] = 4.2;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "failed") << result.dump(2);
    EXPECT_EQ(result["summary"]["failedSteps"], 1);
    EXPECT_EQ(result["summary"]["errorSteps"], 0);
    EXPECT_EQ(result["steps"][0]["outcome"]["data"]["unit"], "V");
    EXPECT_EQ(result["steps"][0]["attempts"][0]["outcome"]["data"]["minimum"], 4.8);
}
TEST_F(DISABLED_PythonIntegrationTests, ExceptionIsErrorAndCleanupRuns)
{
    auto plan = Plan();
    plan["commands"][0]["params"]["fail"] = true;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["summary"]["errorSteps"], 1);
    EXPECT_NE(result.dump().find("Simulated command error"), std::string::npos);
    EXPECT_NE(events.find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, TimeoutIsCooperativeAndCleanupRuns)
{
    auto plan = Plan();
    plan["commands"][0]["params"]["holdMs"] = 2000;
    plan["commands"][0]["policy"]["timeoutMs"] = 100;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "timedOut") << result.dump(2);
    EXPECT_NE(events.find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, CancellationIsCooperativeAndCleanupRuns)
{
    auto plan = Plan();
    plan["commands"][0]["params"]["holdMs"] = 5000;
    const auto result = Run(plan, true);
    EXPECT_EQ(result["status"], "cancelled") << result.dump(2);
    EXPECT_NE(events.find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, PartialInitializationStillShutsDown)
{
    auto plan = Plan();
    plan["instruments"][0]["config"]["failInitialize"] = true;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["summary"]["executedSteps"], 0);
    EXPECT_NE(events.find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, CleanupFailureCannotBecomePassed)
{
    auto plan = Plan();
    plan["instruments"][0]["config"]["failShutdown"] = true;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_NE(result.dump().find("Simulated cleanup failure"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, MultipleInstancesAndFreshSequentialSessions)
{
    auto plan = Plan();
    plan["instruments"].push_back({{"type", "com.artest.python.driver.power"}, {"id", "PS2"}, {"config", Json::object()}});
    auto second = plan["commands"][0];
    second["stepId"] = 2;
    second["instrument"] = "PS2";
    second["params"]["voltage"] = 12.0;
    plan["commands"].push_back(second);
    for (int run = 0; run < 3; ++run)
    {
        const auto result = Run(plan);
        EXPECT_EQ(result["status"], "passed") << result.dump(2);
        EXPECT_EQ(result["steps"][0]["outcome"]["data"]["instrumentId"], "PS1");
        EXPECT_EQ(result["steps"][1]["outcome"]["data"]["instrumentId"], "PS2");
    }
}
TEST_F(DISABLED_PythonIntegrationTests, CrashAfterEffectIsNeverRetriedOrContinued)
{
    const auto marker = packages->Root() / "effect.txt";
    auto plan = Plan("com.artest.command.sample.power-cycle", "com.artest.python.driver.test-faults");
    plan["instruments"][0]["config"] = {{"effectFile", marker.string()}, {"mode", "crash"}};
    plan["commands"][0]["params"] = {{"channel", 1}, {"voltage", 12.0}, {"holdMs", 0}};
    plan["commands"][0]["policy"] = {{"maxAttempts", 3}, {"timeoutMs", 1000}, {"onFailure", "continue"}};
    auto next = plan["commands"][0];
    next["stepId"] = 2;
    plan["commands"].push_back(next);
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["summary"]["totalAttempts"], 1);
    EXPECT_EQ(result["summary"]["skippedSteps"], 1);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
    std::ifstream input(marker);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "effect\n");
    EXPECT_FALSE(std::filesystem::exists(marker.string() + ".cleanup"));
    EXPECT_NE(result.dump().find("PYTHON_CLEANUP_UNCONFIRMED"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, BlockingIoThatIgnoresTimeoutIsTerminatedWithoutReplay)
{
    const auto marker = packages->Root() / "effect.txt";
    auto plan = Plan("com.artest.command.sample.power-cycle", "com.artest.python.driver.test-faults");
    plan["instruments"][0]["config"] = {{"effectFile", marker.string()}, {"mode", "blocking"}, {"blockMs", 30000}};
    plan["commands"][0]["params"] = {{"channel", 1}, {"voltage", 12.0}, {"holdMs", 0}};
    plan["commands"][0]["policy"] = {{"maxAttempts", 3}, {"timeoutMs", 100}, {"onFailure", "continue"}};
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["summary"]["totalAttempts"], 1);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
    EXPECT_FALSE(std::filesystem::exists(marker.string() + ".cleanup"));
}
TEST_F(DISABLED_PythonIntegrationTests, BlockingIoFinishesBeforeDriverCleanupBegins)
{
    const auto marker = packages->Root() / "effect.txt";
    auto plan = Plan("com.artest.command.sample.power-cycle", "com.artest.python.driver.test-faults");
    plan["instruments"][0]["config"] = {{"effectFile", marker.string()}, {"mode", "blocking"}, {"blockMs", 250}};
    plan["commands"][0]["params"] = {{"channel", 1}, {"voltage", 12.0}, {"holdMs", 0}};
    plan["commands"][0]["policy"]["timeoutMs"] = 100;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "timedOut") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], false);
    EXPECT_GE(result["steps"][0]["durationMs"].get<int>(), 200);
    EXPECT_TRUE(std::filesystem::exists(marker.string() + ".cleanup"));
}
TEST_F(DISABLED_PythonIntegrationTests, OfflineCompilationDoesNotRequireAnEnvironment)
{
    EngineClient offline;
    ASSERT_TRUE(offline.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    ASSERT_TRUE(offline.PrepareCatalog(packages->Root().string()).Succeeded());
    ASSERT_TRUE(offline.Compile(Plan().dump()).Succeeded());
}
TEST_F(DISABLED_PythonIntegrationTests, TamperedCodeIsRejectedBeforeExecution)
{
    EngineClient tampered;
    const auto code = packages->Root() / "ARTestPySimulated/code/extension.py";
    std::ofstream(code, std::ios::app) << "\n# unauthorized modification\n";
    ASSERT_TRUE(tampered.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    const auto prepared = tampered.PrepareCatalog(packages->Root().string());
    EXPECT_FALSE(prepared.Succeeded());
    EXPECT_NE(prepared.message.find("hash mismatch"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, MissingEnvironmentFailsWithoutImplicitInstallation)
{
    ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    ASSERT_TRUE(client.PrepareCatalog(packages->Root().string()).Succeeded());
    const auto result = Run(Plan());
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["summary"]["executedSteps"], 0);
    EXPECT_NE(result.dump().find("PYTHON_ENVIRONMENT_REQUIRED"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, EnvironmentCannotBeReboundToAnotherPackage)
{
    std::ifstream input(Repository() / "artifacts/python/environments/python-environments.json");
    auto mapping = Json::parse(input);
    mapping["com.artest.python.simulated"] = mapping["com.artest.python.test-faults"];
    const Json options = {{"loadDefaultCatalog", false}, {"pythonEnvironments", mapping}};
    ASSERT_TRUE(client.Create(options.dump()).Succeeded());
    ASSERT_TRUE(client.PrepareCatalog(packages->Root().string()).Succeeded());
    const auto result = Run(Plan());
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["summary"]["executedSteps"], 0);
    EXPECT_NE(result.dump().find("environment/package mismatch"), std::string::npos);
}

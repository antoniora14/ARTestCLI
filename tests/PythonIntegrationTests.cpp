// Explicit optional gate: scripts/test-python-runtime.ps1 enables this suite.
// Native-only builds neither install Python nor silently count these cases as passed.
#include <ARTestEngineClient.h>
#include <ARTest/Result.h>
#include <gtest/gtest.h>
#include "TestSupport/ReferenceCatalog.h"
#include "TestSupport/C01/Catalog.h"
#include <fstream>
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
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
std::filesystem::path PythonRoot()
{
    wchar_t configured[32768]{};
    if (GetEnvironmentVariableW(L"ARTEST_PYTHON_ROOT", configured, 32768))
        return configured;
    return Repository() / "artifacts/python";
}
class PythonTest : public ::testing::Test
{
  protected:
    std::unique_ptr<artest::tests::ReferenceCatalog> packages;
    mutable std::mutex observationsMutex;
    const std::chrono::steady_clock::time_point observationStart = std::chrono::steady_clock::now();
    std::string events;
    std::vector<std::string> timeline;
    std::atomic_bool stepStarted{false};
    // Keep the client last: its destructor unsubscribes and joins the session
    // before the callback state above is destroyed.
    EngineClient client;

    void Observe(std::string text)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - observationStart).count();
        std::scoped_lock lock{observationsMutex};
        timeline.push_back(std::to_string(elapsed) + " ms | " + std::move(text));
    }
    [[nodiscard]] std::string Observations() const
    {
        std::scoped_lock lock{observationsMutex};
        std::ostringstream output;
        for (const auto &entry : timeline) output << entry << '\n';
        return output.str();
    }
    [[nodiscard]] std::string EventText() const
    {
        std::scoped_lock lock{observationsMutex};
        return events;
    }
    void SetUp() override
    {
        Observe("setup.begin");
        const auto root = Repository();
        const auto python = PythonRoot() / "extensions/ARTestPySimulated";
        ASSERT_TRUE(std::filesystem::is_regular_file(python / "artest-extension.json"))
            << "Run scripts/prepare-python-example.ps1 first.";
        packages = std::make_unique<artest::tests::ReferenceCatalog>(root / "artifacts/extensions/x64" / Binaries().filename());
        artest::tests::effects::AddPackage(packages->Root(), Binaries());
        std::filesystem::copy(python, packages->Root() / "ARTestPySimulated", std::filesystem::copy_options::recursive);
        const auto faults = PythonRoot() / "test-extensions/ARTestPyFaults";
        ASSERT_TRUE(std::filesystem::is_regular_file(faults / "artest-extension.json"));
        std::filesystem::copy(faults, packages->Root() / "ARTestPyFaults", std::filesystem::copy_options::recursive);
        Observe("setup.packages-copied");
        std::ifstream mapping(PythonRoot() / "environments/python-environments.json");
        ASSERT_TRUE(mapping.good()) << "Prepare the example environment mapping first.";
        const Json options = {{"loadDefaultCatalog", false}, {"resultSchemaVersion", 2},
                              {"pythonEnvironments", Json::parse(mapping)}};
        Observe("engine.create.begin");
        auto status = client.Create(options.dump());
        Observe("engine.create.end status=" + std::to_string(status.code) + " message=" + status.message);
        ASSERT_TRUE(status.Succeeded()) << status.message;
        Observe("catalog.prepare.begin");
        status = client.PrepareCatalog(packages->Root().string());
        Observe("catalog.prepare.end status=" + std::to_string(status.code) + " message=" + status.message);
        ASSERT_TRUE(status.Succeeded()) << status.message;
        ASSERT_TRUE(client.SubscribeEvents([this](std::string_view text) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - observationStart).count();
            std::scoped_lock lock{observationsMutex};
            events += text;
            timeline.push_back(std::to_string(elapsed) + " ms | event " + std::string{text});
            if (text.find("Starting step attempt.") != std::string_view::npos) stepStarted = true;
        }).Succeeded());
        Observe("events.subscribed");
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
        Observe("plan.compile.begin");
        auto status = client.Compile(plan.dump());
        Observe("plan.compile.end status=" + std::to_string(status.code) + " message=" + status.message);
        EXPECT_TRUE(status.Succeeded()) << status.message << '\n' << Observations();
        if (!status.Succeeded()) return Json::object();
        Observe("session.start.begin");
        status = client.Start();
        Observe("session.start.end status=" + std::to_string(status.code) + " message=" + status.message);
        EXPECT_TRUE(status.Succeeded()) << status.message << '\n' << Observations();
        if (!status.Succeeded()) return Json::object();
        if (cancel)
        {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{15};
            while (!stepStarted && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            EXPECT_TRUE(stepStarted);
            client.RequestCancel();
            Observe("session.cancel.requested-by-test");
        }
        bool completed = false;
        Observe("session.wait.begin budgetMs=30000");
        status = client.Wait(30000, completed);
        Observe("session.wait.end status=" + std::to_string(status.code) +
                " completed=" + (completed ? "true" : "false") + " message=" + status.message);
        EXPECT_TRUE(status.Succeeded()) << status.message << '\n' << Observations();
        if (!status.Succeeded()) return Json::object();
        if (!completed)
        {
            client.RequestCancel();
            Observe("session.cancel.requested-after-host-timeout");
            bool cleanupCompleted = false;
            const auto cleanupStatus = client.Wait(5000, cleanupCompleted);
            Observe("session.cleanup-wait.end status=" + std::to_string(cleanupStatus.code) +
                    " completed=" + (cleanupCompleted ? "true" : "false") +
                    " message=" + cleanupStatus.message);
            throw std::runtime_error(
                "Session exceeded the 30000 ms host wait budget.\n" + Observations());
        }
        std::string output;
        Observe("result.serialize.begin");
        status = client.SerializeResult(output);
        Observe("result.serialize.end status=" + std::to_string(status.code) + " message=" + status.message);
        EXPECT_TRUE(status.Succeeded()) << status.message << '\n' << Observations();
        if (!status.Succeeded()) return Json::object();
        return Json::parse(output);
    }
};
using DISABLED_PythonIntegrationTests = PythonTest;
}

namespace
{
Json EffectPlan(const std::filesystem::path &marker, bool pythonCommand, bool pythonDriver,
                std::string mode = "lost", std::string action = "propagate")
{
    auto plan = artest::tests::effects::Plan(marker, mode, action);
    if (pythonDriver) plan["instruments"][0]["type"] = "com.artest.python.driver.test-faults";
    if (pythonCommand)
        for (auto &step : plan["commands"]) step["name"] = "com.artest.python.command.test-effects";
    return plan;
}
void AssertEffectStopped(const Json &result, const std::filesystem::path &marker, bool pythonDriver)
{
    ASSERT_TRUE(result.contains("steps")) << "The execution result was unavailable.";
    ASSERT_EQ(result.at("steps").size(), 1u) << result.dump(2);
    EXPECT_EQ(result["summary"]["totalAttempts"], 1);
    EXPECT_EQ(result["summary"]["skippedSteps"], 1);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
    EXPECT_EQ(result["steps"][0]["attempts"][0]["outcome"]["indeterminate"], true);
    EXPECT_NE(result.dump().find(pythonDriver ? "C01 Python lost acknowledgement" :
        "C01 native lost acknowledgement"), std::string::npos);
    EXPECT_EQ(artest::tests::effects::Read(marker), "effect\n");
    EXPECT_EQ(artest::tests::effects::Read(marker.string() + ".calls"), "call\n");
    EXPECT_EQ(artest::tests::effects::Read(marker.string() + ".cleanup"), "cleanup\n");
    // Shutdown completed in the same worker: explicit device uncertainty does
    // not depend on a crash and must not contaminate the cleanup operation.
    EXPECT_EQ(result.dump().find("PYTHON_CLEANUP_UNCONFIRMED"), std::string::npos);
}
}
TEST_F(DISABLED_PythonIntegrationTests, C01PythonCommandWrapsNativeLostAcknowledgement)
{
    const auto marker = packages->Root() / "c01.txt";
    const auto result = Run(EffectPlan(marker, true, false, "lost", "wrap"));
    AssertEffectStopped(result, marker, false);
    EXPECT_NE(result.dump().find("C01 wrapped Python command failure"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, C01NativeCommandWrapsLivePythonDriverUncertainty)
{
    const auto marker = packages->Root() / "c01.txt";
    const auto result = Run(EffectPlan(marker, false, true, "lost", "wrap"));
    AssertEffectStopped(result, marker, true);
    EXPECT_NE(result.dump().find("C01 wrapped command failure"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, C01PythonCommandCannotReplayOrHideSuccess)
{
    const auto marker = packages->Root() / "c01.txt";
    const auto result = Run(EffectPlan(marker, true, true, "lost", "retry"));
    EXPECT_EQ(result["status"], "error");
    AssertEffectStopped(result, marker, true);
}
TEST_F(DISABLED_PythonIntegrationTests, C01NativeCommandCannotReplayLivePythonDriver)
{
    const auto marker = packages->Root() / "c01.txt";
    const auto result = Run(EffectPlan(marker, false, true, "lost", "retry"));
    EXPECT_EQ(result["status"], "error");
    AssertEffectStopped(result, marker, true);
}
TEST_F(DISABLED_PythonIntegrationTests, C01NestedServicesPreserveBothRuntimeDirections)
{
    for (const bool pythonOrigin : {false, true})
    {
        const auto marker = packages->Root() / (pythonOrigin ? "python-origin.txt" : "native-origin.txt");
        const auto relay = packages->Root() / (pythonOrigin ? "native-relay.txt" : "python-relay.txt");
        auto plan = EffectPlan(relay, pythonOrigin, !pythonOrigin, "relay", "wrap");
        plan["instruments"][0]["config"]["relay"] = "PS2";
        auto origin = EffectPlan(marker, false, pythonOrigin)["instruments"][0];
        origin["id"] = "PS2";
        plan["instruments"].push_back(origin);
        const auto result = Run(plan);
        AssertEffectStopped(result, marker, pythonOrigin);
        EXPECT_EQ(artest::tests::effects::Read(relay.string() + ".calls"), "call\n");
        EXPECT_EQ(artest::tests::effects::Read(relay.string() + ".cleanup"), "cleanup\n");
        EXPECT_FALSE(std::filesystem::exists(relay));
    }
}
TEST_F(DISABLED_PythonIntegrationTests, C01LateBlockingResultPreservesUncertaintyAfterDeadline)
{
    const auto marker = packages->Root() / "c01.txt";
    auto plan = EffectPlan(marker, true, true, "blocking-lost");
    plan["commands"][0]["policy"]["timeoutMs"] = 50;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "timedOut");
    AssertEffectStopped(result, marker, true);
}
TEST_F(DISABLED_PythonIntegrationTests, C01ExplicitCancellationAndTimeoutAreOrthogonalToUncertainty)
{
    for (const auto mode : {"cancelled", "timeout"})
    {
        const auto marker = packages->Root() / (std::string(mode) + ".txt");
        const auto result = Run(EffectPlan(marker, false, true, mode));
        EXPECT_EQ(result["status"], std::string(mode) == "timeout" ? "timedOut" : "cancelled");
        AssertEffectStopped(result, marker, true);
    }
}
TEST_F(DISABLED_PythonIntegrationTests, C01PreSendFailureRemainsRetryable)
{
    const auto marker = packages->Root() / "c01.txt";
    const auto result = Run(EffectPlan(marker, true, true, "before"));
    EXPECT_EQ(result["summary"]["totalAttempts"], 6);
    EXPECT_EQ(result["summary"]["executedSteps"], 2);
    EXPECT_FALSE(std::filesystem::exists(marker));
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], false);
    EXPECT_EQ(artest::tests::effects::Read(marker.string() + ".calls"), "call\ncall\ncall\ncall\ncall\ncall\n");
}
TEST_F(DISABLED_PythonIntegrationTests, C01NewPythonSessionAndSeparateInstancesStayClean)
{
    const auto marker = packages->Root() / "c01.txt";
    AssertEffectStopped(Run(EffectPlan(marker, true, true)), marker, true);
    const auto first = packages->Root() / "fresh1.txt", second = packages->Root() / "fresh2.txt";
    auto plan = EffectPlan(first, true, true, "ok");
    auto other = plan["instruments"][0];
    other["id"] = "PS2";
    other["config"]["effectFile"] = second.string();
    plan["instruments"].push_back(other);
    plan["commands"][1]["instrument"] = "PS2";
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "passed") << result.dump(2);
    for (const auto &step : result["steps"]) EXPECT_EQ(step["outcome"]["indeterminate"], false);
    EXPECT_EQ(artest::tests::effects::Read(first), "effect\n");
    EXPECT_EQ(artest::tests::effects::Read(second), "effect\n");
    EXPECT_EQ(artest::tests::effects::Read(first.string() + ".cleanup"), "cleanup\n");
    EXPECT_EQ(artest::tests::effects::Read(second.string() + ".cleanup"), "cleanup\n");
}
TEST_F(DISABLED_PythonIntegrationTests, C01CleanupFailureDoesNotEraseDeviceUncertainty)
{
    const auto marker = packages->Root() / "c01.txt";
    auto plan = EffectPlan(marker, true, true);
    plan["instruments"][0]["config"]["failShutdown"] = true;
    const auto result = Run(plan);
    AssertEffectStopped(result, marker, true);
    EXPECT_NE(result.dump().find("C01 Python cleanup failed"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, PythonCommandInvokesPythonDriver)
{
    const auto result = Run(Plan());
    EXPECT_EQ(result["status"], "passed") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["outcome"]["data"]["value"], 5.0);
    EXPECT_NE(EventText().find("PY_DRIVER_SHUTDOWN"), std::string::npos);
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
    EXPECT_NE(EventText().find("PY_DRIVER_SHUTDOWN"), std::string::npos);
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
    EXPECT_NE(EventText().find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, TimeoutIsCooperativeAndCleanupRuns)
{
    auto plan = Plan();
    plan["commands"][0]["params"]["holdMs"] = 2000;
    plan["commands"][0]["policy"]["timeoutMs"] = 100;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "timedOut") << result.dump(2);
    EXPECT_NE(EventText().find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, CancellationIsCooperativeAndCleanupRuns)
{
    auto plan = Plan();
    plan["commands"][0]["params"]["holdMs"] = 5000;
    const auto result = Run(plan, true);
    EXPECT_EQ(result["status"], "cancelled") << result.dump(2);
    EXPECT_NE(EventText().find("PY_DRIVER_SHUTDOWN"), std::string::npos);
}
TEST_F(DISABLED_PythonIntegrationTests, PartialInitializationStillShutsDown)
{
    auto plan = Plan();
    plan["instruments"][0]["config"]["failInitialize"] = true;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["summary"]["executedSteps"], 0);
    EXPECT_NE(EventText().find("PY_DRIVER_SHUTDOWN"), std::string::npos);
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
    std::ifstream input(PythonRoot() / "environments/python-environments.json");
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

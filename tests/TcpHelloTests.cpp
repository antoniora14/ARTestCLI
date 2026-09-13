#include "TestSupport/TcpServerProcess.h"
#include "../examples/ARTestTcpHello/TcpVoltageDriver.h"
#include <ARTest/Testing.h>
#include <ARTestEngineClient.h>
#include <gtest/gtest.h>
#include <future>

using namespace artest::tcphello;
using namespace artest::tests::tcp;
namespace sdk = artest::sdk;
using artest::sdk::Json;
using artest::sdk::Parameters;
using artest::sdk::Result;
using artest::sdk::Status;
using artest::sdk::testing::TestContext;
namespace
{
Json Configuration(unsigned short port, int budget = 200)
{
    return {{"port", port}, {"initializeMs", 2000}, {"operationMs", budget}, {"shutdownMs", 200}};
}
Result ApplyVoltage(TcpVoltageDriver& driver, sdk::Context& context, double value = 12)
{
    const Json request{{"voltage", value}};
    return driver.Dispatch(Apply, Parameters(request), context);
}
Json Plan(unsigned short port)
{
    Json step{{"stepId", 1}, {"name", "com.artest.example.command.tcp-set-and-measure"},
        {"instrument", "V1"}, {"params", {{"voltage", 12}, {"minimum", 11.9}, {"maximum", 12.1}}},
        {"policy", {{"maxAttempts", 3}, {"timeoutMs", 2000}, {"onFailure", "continue"}}}};
    auto next = step;
    next["stepId"] = 2;
    return {{"format", "ARTest.Script"}, {"version", 1},
        {"instruments", Json::array({{{"id", "V1"}, {"type", "com.artest.example.driver.tcp-voltage"},
            {"config", Configuration(port)}}})}, {"commands", Json::array({step, next})}};
}
class Session
{
public:
    std::string events;
    sdk::EngineClient client; // Unsubscribe/destroy before callback storage, including failed waits.
    explicit Session(const std::filesystem::path& root, const Json& options = Json::object())
    {
        auto settings = options;
        settings["loadDefaultCatalog"] = false;
        settings["resultSchemaVersion"] = 2;
        auto status = client.Create(settings.dump());
        if (!status.Succeeded()) throw std::runtime_error(status.message);
        status = client.PrepareCatalog(root.string());
        if (!status.Succeeded()) throw std::runtime_error(status.message);
        status = client.SubscribeEvents([this](std::string_view event) { events += event; });
        if (!status.Succeeded()) throw std::runtime_error(status.message);
    }
    Json Run(const Json& plan, ServerProcess* cancelAfterEffect = nullptr)
    {
        auto status = client.Compile(plan.dump());
        if (!status.Succeeded()) throw std::runtime_error(status.message);
        status = client.Start();
        if (!status.Succeeded()) throw std::runtime_error(status.message);
        if (cancelAfterEffect)
        {
            const auto end = Clock::now() + 5s;
            while (!cancelAfterEffect->Effects() && Clock::now() < end) std::this_thread::sleep_for(2ms);
            EXPECT_EQ(cancelAfterEffect->Effects(), 1u);
            client.RequestCancel();
        }
        bool complete = false;
        status = client.Wait(10000, complete);
        if (!status.Succeeded() || !complete) throw std::runtime_error("C02 session did not complete.");
        std::string result;
        status = client.SerializeResult(result);
        if (!status.Succeeded()) throw std::runtime_error(status.message);
        const auto parsed = Json::parse(result);
        wchar_t evidence[32768]{};
        if (GetEnvironmentVariableW(L"ARTEST_C02_EVIDENCE_ROOT", evidence, 32768))
        {
            static std::atomic_uint next{0};
            const auto file = std::filesystem::path(evidence) /
                ("session-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(next++) + ".json");
            std::ofstream output(file);
            output << Json{{"plan", plan}, {"result", parsed}, {"events", events}}.dump(2);
            output.flush();
            if (!output) throw std::runtime_error("Cannot retain session evidence.");
        }
        return parsed;
    }
};
std::filesystem::path Catalog()
{
    return Repository() / "examples/ARTestTcpHello/out/extensions/x64" / Binaries().filename();
}
}
TEST(TcpHelloTests, TwoInstancesHaveIndependentStateAndTypedMeasurements)
{
    ServerProcess server;
    TestContext context;
    TcpVoltageDriver first, second;
    const auto config = Configuration(server.Port());
    ASSERT_TRUE(first.Initialize(Parameters(config), context));
    ASSERT_TRUE(second.Initialize(Parameters(config), context));
    ASSERT_TRUE(ApplyVoltage(first, context, 12));
    ASSERT_TRUE(ApplyVoltage(second, context, 5));
    const Json empty = Json::object();
    const auto reading = first.Dispatch(Read, Parameters(empty), context);
    ASSERT_TRUE(reading);
    EXPECT_EQ(reading.SchemaId(), MeasurementSchema);
    EXPECT_EQ(reading.Data()->at("value"), 12);
    EXPECT_EQ(server.Effects(), 2u);
    EXPECT_TRUE(first.Shutdown(context));
    EXPECT_TRUE(second.Shutdown(context));
}
TEST(TcpHelloTests, FragmentedAcknowledgementIsAcceptedWithinOneBudget)
{
    ServerProcess server(L"fragmented");
    TestContext context;
    TcpVoltageDriver driver;
    auto config = Configuration(server.Port(), 2000);
    config["shutdownMs"] = 2000;
    ASSERT_TRUE(driver.Initialize(Parameters(config), context));
    ASSERT_TRUE(ApplyVoltage(driver, context));
    EXPECT_TRUE(driver.Shutdown(context));
}
TEST(TcpHelloTests, InvalidAndMissingAcknowledgementsInvalidateTheConnection)
{
    for (const auto mode : {L"lost-ack", L"disconnect", L"late", L"slow-drip",
                            L"malformed", L"oversized", L"wrong-id", L"wrong-op", L"wrong-version"})
    {
        SCOPED_TRACE(std::filesystem::path(mode).string());
        ServerProcess server(mode);
        TestContext context;
        TcpVoltageDriver driver;
        const auto config = Configuration(server.Port(), 150);
        ASSERT_TRUE(driver.Initialize(Parameters(config), context));
        const auto start = Clock::now();
        const auto result = ApplyVoltage(driver, context);
        EXPECT_FALSE(result);
        EXPECT_TRUE(result.IsIndeterminate());
        EXPECT_LT(Clock::now() - start, 2s);
        EXPECT_EQ(server.Effects(), 1u);
        const auto next = ApplyVoltage(driver, context, 5);
        EXPECT_FALSE(next);
        EXPECT_FALSE(next.IsIndeterminate());
        EXPECT_EQ(next.Code(), Status::InvalidState);
        EXPECT_EQ(server.Effects(), 1u);
        EXPECT_FALSE(driver.Shutdown(context));
    }
}
TEST(TcpHelloTests, LateReadIsNotAnUncertainWriteAndCannotContaminateNextOperation)
{
    ServerProcess server(L"late-read");
    TestContext context;
    TcpVoltageDriver driver;
    const auto config = Configuration(server.Port(), 100);
    ASSERT_TRUE(driver.Initialize(Parameters(config), context));
    const Json empty = Json::object();
    auto result = driver.Dispatch(Read, Parameters(empty), context);
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.IsIndeterminate());
    EXPECT_EQ(result.Code(), Status::TimedOut);
    result = ApplyVoltage(driver, context);
    EXPECT_EQ(result.Code(), Status::InvalidState);
    EXPECT_EQ(server.Effects(), 0u);
    EXPECT_FALSE(driver.Shutdown(context));
}
TEST(TcpHelloTests, CancelledBeforeSendDoesNotInventAnEffect)
{
    ServerProcess server;
    TestContext context;
    TcpVoltageDriver driver;
    const auto config = Configuration(server.Port());
    ASSERT_TRUE(driver.Initialize(Parameters(config), context));
    context.cancelled = true;
    auto result = ApplyVoltage(driver, context);
    EXPECT_EQ(result.Code(), Status::Cancelled);
    EXPECT_FALSE(result.IsIndeterminate());
    EXPECT_EQ(server.Effects(), 0u);
    context.cancelled = false;
    EXPECT_TRUE(driver.Shutdown(context));
}
TEST(TcpHelloTests, RefusedConnectionAndPartialInitializationRemainBounded)
{
    Winsock winsock;
    Socket reservation(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(reservation.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    int size = sizeof(address);
    ASSERT_EQ(getsockname(reservation.Get(), reinterpret_cast<sockaddr*>(&address), &size), 0);
    TestContext context;
    TcpVoltageDriver driver;
    const auto config = Configuration(ntohs(address.sin_port));
    const auto start = Clock::now();
    auto result = driver.Initialize(Parameters(config), context);
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.IsIndeterminate());
    EXPECT_LT(Clock::now() - start, 3s);
    EXPECT_FALSE(driver.Shutdown(context));
    ServerProcess server(L"partial-init");
    const auto partial = Configuration(server.Port());
    EXPECT_FALSE(driver.Initialize(Parameters(partial), context));
    EXPECT_FALSE(driver.Shutdown(context));
    EXPECT_EQ(server.Effects(), 0u);
}
TEST(TcpHelloTests, CleanupTimeoutDoesNotBecomeSuccess)
{
    ServerProcess server(L"close-timeout");
    TestContext context;
    TcpVoltageDriver driver;
    const auto config = Configuration(server.Port());
    ASSERT_TRUE(driver.Initialize(Parameters(config), context));
    const auto start = Clock::now();
    const auto result = driver.Shutdown(context);
    EXPECT_EQ(result.Code(), Status::TimedOut);
    EXPECT_LT(Clock::now() - start, 2s);
    EXPECT_FALSE(result.IsIndeterminate());
}
TEST(TcpHelloTests, OfflineCompilationDoesNotConnect)
{
    ServerProcess server;
    Session session(Catalog());
    auto status = session.client.Compile(Plan(server.Port()).dump());
    EXPECT_TRUE(status.Succeeded()) << status.message;
    const auto journal = server.Journal();
    ASSERT_EQ(journal.size(), 1u);
    EXPECT_EQ(journal[0]["event"], "listening");
}
TEST(TcpHelloTests, LostAckStopsRetryAndContinueUsingServerSideEffectEvidence)
{
    ServerProcess server(L"lost-ack");
    Session session(Catalog());
    const auto result = session.Run(Plan(server.Port()));
    ASSERT_EQ(result["steps"].size(), 1u) << result.dump(2);
    EXPECT_EQ(result["summary"]["totalAttempts"], 1);
    EXPECT_EQ(result["summary"]["skippedSteps"], 1);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
    EXPECT_EQ(result["steps"][0]["attempts"][0]["outcome"]["indeterminate"], true);
    EXPECT_EQ(server.Effects(), 1u);
    EXPECT_NE(session.events.find("TCP_HELLO_CLEANUP_ATTEMPTED"), std::string::npos);
}
TEST(TcpHelloTests, ProvenPreSendRejectionKeepsNormalRetryAndContinuePolicy)
{
    ServerProcess server;
    const auto catalog = server.Root() / "catalog";
    std::filesystem::create_directory(catalog);
    std::filesystem::copy(Catalog() / "ARTestTcpHello", catalog / "ARTestTcpHello", std::filesystem::copy_options::recursive);
    std::filesystem::copy(Repository() / "artifacts/c02-test-packages/x64" / Binaries().filename() / "C02FaultCommands",
        catalog / "C02FaultCommands", std::filesystem::copy_options::recursive);
    Session session(catalog);
    auto plan = Plan(server.Port());
    for (auto& step : plan["commands"])
    {
        step["name"] = "com.artest.test.command.tcp-negative";
        step["params"] = Json::object();
    }
    const auto result = session.Run(plan);
    EXPECT_EQ(result["summary"]["totalAttempts"], 6) << result.dump(2);
    EXPECT_EQ(result["summary"]["executedSteps"], 2);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], false);
    EXPECT_EQ(server.Effects(), 0u);
    EXPECT_NE(session.events.find("TCP_HELLO_CLEANUP_ATTEMPTED"), std::string::npos);
}
TEST(TcpHelloTests, CancellationAfterAppliedWritePreservesUncertainty)
{
    ServerProcess server(L"lost-ack");
    Session session(Catalog());
    auto plan = Plan(server.Port());
    plan["instruments"][0]["config"]["operationMs"] = 5000;
    const auto result = session.Run(plan, &server);
    EXPECT_EQ(result["status"], "error") << result.dump(2); // Separate unconfirmed cleanup.
    EXPECT_EQ(result["steps"][0]["status"], "cancelled");
    EXPECT_EQ(result["steps"][0]["attempts"][0]["status"], "cancelled");
    EXPECT_EQ(result["summary"]["totalAttempts"], 1);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
    EXPECT_EQ(server.Effects(), 1u);
    EXPECT_NE(session.events.find("TCP_HELLO_CLEANUP_ATTEMPTED"), std::string::npos);
}
TEST(TcpHelloTests, EngineTimeoutAfterWritePreservesUncertainty)
{
    ServerProcess server(L"lost-ack");
    Session session(Catalog());
    auto plan = Plan(server.Port());
    plan["commands"][0]["policy"]["timeoutMs"] = 100;
    plan["instruments"][0]["config"]["operationMs"] = 5000;
    const auto result = session.Run(plan);
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["status"], "timedOut");
    EXPECT_EQ(result["steps"][0]["attempts"][0]["status"], "timedOut");
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
    EXPECT_EQ(server.Effects(), 1u);
}
TEST(TcpHelloTests, LaterSessionIsCleanAndFailedLimitIsNotPassed)
{
    ServerProcess server;
    Session session(Catalog());
    auto plan = Plan(server.Port());
    auto result = session.Run(plan);
    EXPECT_EQ(result["status"], "passed") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["outcome"]["data"]["value"], 12);
    EXPECT_EQ(result["steps"][0]["outcome"]["dataSchema"], "artest.schema.example.tcp-measurement.v1");
    plan["commands"][0]["params"]["minimum"] = 13;
    plan["commands"][0]["params"]["maximum"] = 14;
    result = session.Run(plan);
    EXPECT_EQ(result["status"], "failed") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], false);
}
TEST(TcpHelloTests, BackpressuredWriteHasOneDeadlineAndObservesCancellation)
{
    Winsock winsock;
    Socket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(listener.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    ASSERT_EQ(listen(listener.Get(), 2), 0);
    int size = sizeof(address);
    ASSERT_EQ(getsockname(listener.Get(), reinterpret_cast<sockaddr*>(&address), &size), 0);
    for (bool cancel : {false, true})
    {
        Budget connectBudget(1000, [] { return Result::Success(); });
        auto socket = Connect(ntohs(address.sin_port), connectBudget);
        Socket peer(accept(listener.Get(), nullptr, nullptr)); // Connected peer deliberately never reads.
        ASSERT_TRUE(peer);
        int small = 4096;
        ASSERT_EQ(setsockopt(socket.Get(), SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&small), sizeof(small)), 0);
        const auto start = Clock::now();
        Budget budget(150, [start, cancel]
        {
            return cancel && Clock::now() - start > 75ms ?
                Result::Failure(Status::Cancelled, "Test write cancellation") : Result::Success();
        });
        bool sent = false;
        try
        {
            Send(socket.Get(), std::string(16 * 1024 * 1024, 'x'), budget, sent);
            FAIL() << "Backpressured transport write unexpectedly completed.";
        }
        catch (const Failure& error) { EXPECT_EQ(error.cause, cancel ? Status::Cancelled : Status::TimedOut); }
        EXPECT_TRUE(sent);
        EXPECT_LT(Clock::now() - start, 2s);
    }
}
TEST(TcpHelloTests, ConcurrentTransactionsRemainSerialized)
{
    ServerProcess server(L"fragmented");
    TestContext firstContext, secondContext;
    TcpVoltageDriver driver;
    auto config = Configuration(server.Port(), 2000);
    config["shutdownMs"] = 2000;
    ASSERT_TRUE(driver.Initialize(Parameters(config), firstContext));
    auto first = std::async(std::launch::async, [&] { return ApplyVoltage(driver, firstContext, 12); });
    auto second = ApplyVoltage(driver, secondContext, 5);
    auto result = first.get();
    ASSERT_TRUE(result);
    ASSERT_TRUE(second);
    EXPECT_EQ(result.Data()->at("value"), 12);
    EXPECT_EQ(second.Data()->at("value"), 5);
    EXPECT_EQ(server.Effects(), 2u);
    EXPECT_TRUE(driver.Shutdown(firstContext));
}
TEST(TcpHelloTests, CleanupWaitsForActiveTransactionWithoutClosingItsSocket)
{
    ServerProcess server(L"lost-ack");
    TestContext callContext, cleanupContext;
    TcpVoltageDriver driver;
    auto config = Configuration(server.Port(), 100);
    config["shutdownMs"] = 1000;
    ASSERT_TRUE(driver.Initialize(Parameters(config), callContext));
    auto call = std::async(std::launch::async, [&] { return ApplyVoltage(driver, callContext); });
    const auto end = Clock::now() + 2s;
    while (!server.Effects() && Clock::now() < end) std::this_thread::sleep_for(2ms);
    ASSERT_EQ(server.Effects(), 1u);
    EXPECT_FALSE(driver.Shutdown(cleanupContext));
    const auto result = call.get();
    EXPECT_TRUE(result.IsIndeterminate());
    EXPECT_EQ(result.Code(), Status::TimedOut);
}
namespace
{
std::filesystem::path PythonRoot()
{
    wchar_t configured[32768]{};
    if (GetEnvironmentVariableW(L"ARTEST_TCP_PYTHON_ROOT", configured, 32768)) return configured;
    return Repository() / "artifacts/python-c02";
}
Json RunPython(ServerProcess& server, bool failedLimit = false, bool cancel = false)
{
    const auto catalog = server.Root() / "catalog";
    std::filesystem::create_directory(catalog);
    std::filesystem::copy(Catalog() / "ARTestTcpHello", catalog / "ARTestTcpHello", std::filesystem::copy_options::recursive);
    std::filesystem::copy(PythonRoot() / "extensions/ARTestPyTcpHello", catalog / "ARTestPyTcpHello", std::filesystem::copy_options::recursive);
    std::ifstream input(PythonRoot() / "python-environments.json");
    if (!input) throw std::runtime_error("Run prepare-tcp-hello-python.ps1 before this explicit optional gate.");
    Session session(catalog, {{"pythonEnvironments", Json::parse(input)}});
    auto plan = Plan(server.Port());
    for (auto& step : plan["commands"]) step["name"] = "com.artest.example.python.command.tcp-set-and-measure";
    if (failedLimit)
    {
        plan["commands"][0]["params"]["minimum"] = 13;
        plan["commands"][0]["params"]["maximum"] = 14;
    }
    if (cancel) plan["instruments"][0]["config"]["operationMs"] = 5000;
    const auto result = session.Run(plan, cancel ? &server : nullptr);
    EXPECT_NE(session.events.find("TCP_HELLO_CLEANUP_ATTEMPTED"), std::string::npos);
    return result;
}
}
TEST(DISABLED_TcpHelloPythonTests, NativeDriverReturnsTypedMeasurements)
{
    ServerProcess server;
    const auto result = RunPython(server);
    EXPECT_EQ(result["status"], "passed") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["outcome"]["data"]["value"], 12);
    EXPECT_EQ(result["steps"][0]["outcome"]["dataSchema"], "artest.schema.example.tcp-measurement.v1");
    EXPECT_EQ(server.Effects(), 2u);
}
TEST(DISABLED_TcpHelloPythonTests, FailedLimitRemainsFailed)
{
    ServerProcess server;
    const auto result = RunPython(server, true);
    EXPECT_EQ(result["status"], "failed") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], false);
}
TEST(DISABLED_TcpHelloPythonTests, LostAckIsNotRetried)
{
    ServerProcess server(L"lost-ack");
    const auto result = RunPython(server);
    EXPECT_EQ(result["summary"]["totalAttempts"], 1) << result.dump(2);
    EXPECT_EQ(result["summary"]["skippedSteps"], 1);
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
    EXPECT_EQ(result["steps"][0]["attempts"][0]["outcome"]["indeterminate"], true);
    EXPECT_EQ(server.Effects(), 1u);
}
TEST(DISABLED_TcpHelloPythonTests, CancellationAfterEffectPreservesUncertainty)
{
    ServerProcess server(L"lost-ack");
    const auto result = RunPython(server, false, true);
    EXPECT_EQ(result["status"], "error") << result.dump(2);
    EXPECT_EQ(result["steps"][0]["status"], "cancelled");
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
    EXPECT_EQ(server.Effects(), 1u);
}

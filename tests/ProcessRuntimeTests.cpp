#include "ARTestEngine.Process/WorkerSupervisor.h"
#include "ARTestEngine.Process/ManagedPackage.h"
#include "ARTestEngine/Extensions/ComponentAdapters.h"
#include "TestSupport/RecordingEventSink.h"
#include "ThirdParty/json.hpp"
#include <gtest/gtest.h>
#include <atomic>

using namespace artest::process;
using namespace std::chrono_literals;
namespace
{
WorkerOptions Options(std::wstring mode = L"normal")
{
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return {path, {L"--artest-process-worker", std::move(mode)},
            "com.artest.process.fixture", std::string(64, 'a'), 1500ms, 100ms};
}
wire::Request Request(std::string operation = "echo")
{
    wire::Request request;
    request.set_operation(wire::INVOKE);
    request.set_operation_id(std::move(operation));
    return request;
}
bool Exited(DWORD pid)
{
    UniqueHandle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    return !process || WaitForSingleObject(process.Get(), 1000) == WAIT_OBJECT_0;
}
}
TEST(ProcessProtocolTests, GoldenMessageAndUnknownOptionalFieldRoundTrip)
{
    auto message = Message(1, 2);
    message.mutable_request()->set_operation(wire::DESCRIBE);
    const std::string golden("\x10\x02\x18\x01\x20\x02\x5a\x02\x08\x01", 10);
    EXPECT_EQ(Encode(message), golden);
    EXPECT_EQ(Decode(golden).request().operation(), wire::DESCRIBE);
    // Field 100 (varint) may be ignored; an unknown mandatory body may not.
    EXPECT_EQ(Decode(golden + std::string("\xa0\x06\x01", 3)).correlation(), 2u);
}
TEST(ProcessProtocolTests, UncertaintyRequiresWire02AndUnsuccessfulStatus)
{
    const std::string oldGolden("\x10\x01\x18\x01\x20\x02\x5a\x02\x08\x01", 10);
    EXPECT_THROW(Decode(oldGolden), ProcessError);
    auto message = Message(1, 2);
    message.mutable_response()->set_status(wire::TIMED_OUT);
    message.mutable_response()->set_effect_indeterminate(true);
    EXPECT_TRUE(Decode(Encode(message)).response().effect_indeterminate());
    message.mutable_response()->set_status(wire::OK);
    EXPECT_THROW(Encode(message), ProcessError);
    auto manifest = nlohmann::json{{"not", "a managed manifest"}};
    EXPECT_THROW(ParseManagedPackageRequirements(manifest), ProcessError);
}
TEST(ProcessProtocolTests, RejectsMalformedVersionsBodiesJsonAndSize)
{
    EXPECT_THROW(Decode(std::string("\xff", 1)), ProcessError);
    auto message = Message(1, 2);
    EXPECT_THROW(Encode(message), ProcessError);
    message.mutable_request()->set_operation(wire::INVOKE);
    message.set_minor(99);
    EXPECT_THROW(Encode(message), ProcessError);
    message.set_minor(ProtocolMinor);
    message.mutable_request()->mutable_payload()->set_schema_id("test");
    message.mutable_request()->mutable_payload()->set_json("{\"x\":NaN}");
    EXPECT_THROW(Encode(message), ProcessError);
    EXPECT_THROW(Decode(std::string(MaxFrameBytes + 1, 'x')), ProcessError);
}
namespace
{
nlohmann::json ManagedManifest()
{
    return {{"schemaVersion", 3}, {"runtime", {
        {"kind", "python"}, {"entry", "python"}, {"entryPoint", "example.definition:create_extension"},
        {"isolation", "outOfProcess"}, {"architecture", "x64"}, {"protocol", {{"major", 0}, {"minor", ProtocolMinor}}},
        {"runtimeVersion", "3.13"}, {"dependencyLock", "requirements.lock"}}},
        {"inventory", nlohmann::json::array({
            {{"path", "python/example/definition.py"}, {"sha256", std::string(64, 'a')}},
            {{"path", "requirements.lock"}, {"sha256", std::string(64, 'b')}}})}};
}
}
TEST(ManagedManifestTests, ProjectsPythonAndDotNetRequirementsWithoutExecutingCode)
{
    auto manifest = ManagedManifest();
    const auto python = ParseManagedPackageRequirements(manifest);
    EXPECT_EQ(python.runtime.kind, ManagedRuntimeKind::Python);
    EXPECT_EQ(python.files.size(), 2u);
    manifest["runtime"]["kind"] = "dotnet";
    manifest["runtime"]["runtimeVersion"] = "10.0";
    manifest["runtime"]["entry"] = "Example.dll";
    manifest["runtime"]["entryPoint"] = "Example.Extension.Entry";
    manifest["inventory"][0]["path"] = "Example.dll";
    EXPECT_EQ(ParseManagedPackageRequirements(manifest).runtime.kind, ManagedRuntimeKind::DotNet);
}
TEST(ManagedManifestTests, RejectsUnsafePathsDuplicateInventoryAndUnsupportedRequirements)
{
    for (const auto bad : {"../outside", "D:/outside", "a/../b", "CON", "a\\b", "/root", "a//b", "a."})
    {
        auto manifest = ManagedManifest();
        manifest["runtime"]["entry"] = bad;
        EXPECT_THROW(ParseManagedPackageRequirements(manifest), ProcessError) << bad;
    }
    auto manifest = ManagedManifest();
    manifest["inventory"].push_back(manifest["inventory"][0]);
    EXPECT_THROW(ParseManagedPackageRequirements(manifest), ProcessError);
    manifest = ManagedManifest();
    manifest["runtime"]["runtimeVersion"] = "3.7";
    EXPECT_THROW(ParseManagedPackageRequirements(manifest), ProcessError);
    manifest = ManagedManifest();
    manifest["runtime"]["protocol"]["minor"] = ProtocolMinor + 1;
    EXPECT_THROW(ParseManagedPackageRequirements(manifest), ProcessError);
    manifest["runtime"]["protocol"]["minor"] = 1;
    EXPECT_THROW(ParseManagedPackageRequirements(manifest), ProcessError);
    manifest = ManagedManifest();
    manifest["inventory"].erase(manifest["inventory"].begin() + 1);
    EXPECT_THROW(ParseManagedPackageRequirements(manifest), ProcessError);
}
TEST(ProcessWorkerTests, HandshakeRepeatedCallsAndGracefulStop)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    EXPECT_EQ(worker.State(), WorkerState::Ready);
    EXPECT_NE(worker.Generation(), 0u);
    EXPECT_EQ(worker.Call(Request(), 1s).status(), wire::OK);
    EXPECT_EQ(worker.Call(Request("fragmented"), 1s).status(), wire::OK);
    const auto pid = worker.ProcessId();
    worker.Stop();
    EXPECT_EQ(worker.State(), WorkerState::Exited);
    EXPECT_TRUE(Exited(pid));
}
TEST(ProcessWorkerTests, NestedBidirectionalServicesDoNotDeadlockOrResetBudget)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    int calls = 0;
    worker.SetServiceHandler([&](const wire::Request &request, WorkerSupervisor &session) {
        ++calls;
        EXPECT_EQ(request.operation(), wire::INVOKE_SERVICE);
        return session.Call(Request("nested"), 10s);
    });
    const auto response = worker.Call(Request("service"), 1s);
    EXPECT_EQ(response.status(), wire::OK);
    EXPECT_EQ(calls, 1);
    const auto payload = nlohmann::json::parse(response.payload().json());
    EXPECT_EQ(payload["operation"], "nested");
    EXPECT_LE(payload["remainingMs"].get<int>(), 1000);
}
TEST(ProcessWorkerTests, CyclicServiceRequestsFailClosed)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    worker.SetServiceHandler([](const wire::Request &, WorkerSupervisor &session) {
        return session.Call(Request("service"), 1s);
    });
    try { worker.Call(Request("service"), 1s); FAIL() << "Expected cycle rejection"; }
    catch (const ProcessError &error) { EXPECT_EQ(error.code, "PROCESS_SERVICE_CYCLE"); }
    EXPECT_EQ(worker.State(), WorkerState::Faulted);
    EXPECT_TRUE(Exited(worker.ProcessId()));
}
TEST(ProcessWorkerTests, CancellationRejectsNewServiceWork)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    bool cancelled = false;
    int calls = 0;
    worker.SetEventHandler([&](const wire::Event &) { cancelled = true; });
    worker.SetServiceHandler([&](const wire::Request &, WorkerSupervisor &) {
        ++calls;
        return Response(wire::OK);
    });
    EXPECT_EQ(worker.Call(Request("cancel-service"), 1s, [&] { return cancelled; }).status(), wire::CANCELLED);
    EXPECT_EQ(calls, 0);
}
TEST(ProcessWorkerTests, StopInsideCallbackFailsClosedWithoutDanglingTransport)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    worker.SetServiceHandler([](const wire::Request &, WorkerSupervisor &session) {
        session.Stop();
        return Response(wire::OK);
    });
    EXPECT_THROW(worker.Call(Request("service"), 1s), ProcessError);
    EXPECT_EQ(worker.State(), WorkerState::Faulted);
    EXPECT_TRUE(Exited(worker.ProcessId()));
}
TEST(ProcessWorkerTests, CancellationAndTimeoutAreDistinctFromAcknowledgement)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    const auto start = Clock::now();
    EXPECT_EQ(worker.Call(Request("wait"), 1s, [start] { return Clock::now() - start > 30ms; }).status(),
              wire::CANCELLED);
    EXPECT_EQ(worker.Call(Request("wait"), 30ms).status(), wire::TIMED_OUT);
    EXPECT_EQ(worker.Call(Request(), 1s).status(), wire::OK);
}
TEST(ProcessWorkerTests, InterruptedResponsePreservesUncertaintyPayloadAndDiagnostic)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    for (const bool cancel : {false, true})
    {
        const auto start = Clock::now();
        const auto result = worker.Call(Request("uncertain-wait"), cancel ? 1s : 30ms,
            [start, cancel] { return cancel && Clock::now() - start > 30ms; });
        EXPECT_EQ(result.status(), cancel ? wire::CANCELLED : wire::TIMED_OUT);
        EXPECT_TRUE(result.effect_indeterminate());
        EXPECT_NE(result.diagnostic().find("C01 lost acknowledgement"), std::string::npos);
        EXPECT_EQ(result.payload().json(), R"({"effect":"unconfirmed"})");
    }
    EXPECT_EQ(worker.Call(Request(), 1s).status(), wire::OK);
}
TEST(ProcessWorkerTests, PredispatchCancellationDoesNotCallWorker)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    EXPECT_EQ(worker.Call(Request("crash"), 1s, [] { return true; }).status(), wire::CANCELLED);
    EXPECT_EQ(worker.Call(Request(), 1s).status(), wire::OK);
}
TEST(ProcessWorkerTests, ParentCancellationPropagatesIntoNestedServiceWait)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    worker.SetServiceHandler([](const wire::Request &, WorkerSupervisor &session) {
        return session.Call(Request("wait"), 10s);
    });
    const auto start = Clock::now();
    EXPECT_EQ(worker.Call(Request("service"), 2s, [start] { return Clock::now() - start > 30ms; }).status(),
              wire::CANCELLED);
    EXPECT_LT(Clock::now() - start, 1s);
}
TEST(ProcessWorkerTests, ForcedTerminationIncludesOwnedDescendants)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    const auto pid = static_cast<DWORD>(worker.Call(Request("spawn-child"), 1s).handle());
    UniqueHandle child(OpenProcess(SYNCHRONIZE, FALSE, pid));
    ASSERT_TRUE(child);
    EXPECT_THROW(worker.Call(Request("stall"), 30ms), ProcessError);
    EXPECT_EQ(WaitForSingleObject(child.Get(), 2000), WAIT_OBJECT_0);
}
TEST(ProcessWorkerTests, GracefulWorkerExitWithLiveDescendantIsNotConfirmedCleanup)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    const auto pid = static_cast<DWORD>(worker.Call(Request("spawn-child"), 1s).handle());
    UniqueHandle child(OpenProcess(SYNCHRONIZE, FALSE, pid));
    ASSERT_TRUE(child);
    worker.Stop();
    EXPECT_EQ(worker.State(), WorkerState::Faulted);
    EXPECT_EQ(WaitForSingleObject(child.Get(), 2000), WAIT_OBJECT_0);
}
TEST(ProcessWorkerTests, CancelAckWithoutCompletionTerminatesWorker)
{
    WorkerSupervisor worker(Options(L"ignore-cancel"));
    worker.Start();
    try { worker.Call(Request("wait"), 30ms); FAIL() << "Expected bounded termination"; }
    catch (const ProcessError &error)
    {
        EXPECT_EQ(error.code, "PROCESS_CANCEL_TIMEOUT");
        EXPECT_NE(std::string(error.what()).find("Cleanup is unconfirmed"), std::string::npos);
    }
    EXPECT_TRUE(Exited(worker.ProcessId()));
}
TEST(ProcessWorkerTests, BlockedUserCodeCannotHoldEngineIndefinitely)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    const auto start = Clock::now();
    EXPECT_THROW(worker.Call(Request("stall"), 30ms), ProcessError);
    EXPECT_LT(Clock::now() - start, 2s);
    EXPECT_EQ(worker.State(), WorkerState::Faulted);
    EXPECT_TRUE(Exited(worker.ProcessId()));
}
TEST(ProcessWorkerTests, CrashNeverReplaysAnOperation)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    const auto generation = worker.Generation();
    EXPECT_THROW(worker.Call(Request("crash"), 1s), ProcessError);
    EXPECT_EQ(worker.State(), WorkerState::Faulted);
    EXPECT_THROW(worker.Call(Request(), 1s), ProcessError);
    EXPECT_EQ(worker.Generation(), generation);
}
TEST(ProcessWorkerTests, MalformedLengthAndStaleGenerationAreRejected)
{
    for (const auto operation : {"oversized", "stale"})
    {
        WorkerSupervisor worker(Options());
        worker.Start();
        EXPECT_THROW(worker.Call(Request(operation), 1s), ProcessError);
        EXPECT_EQ(worker.State(), WorkerState::Faulted);
        EXPECT_TRUE(Exited(worker.ProcessId()));
    }
}
TEST(ProcessWorkerTests, DuplicateTerminalResponseIsNotAcceptedByNextInvocation)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    EXPECT_EQ(worker.Call(Request("duplicate"), 1s).status(), wire::OK);
    EXPECT_THROW(worker.Call(Request(), 1s), ProcessError);
}
TEST(ProcessWorkerTests, LogOverflowCannotHideTerminalResult)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    std::size_t events = 0;
    worker.SetEventHandler([&](const wire::Event &) { ++events; });
    EXPECT_EQ(worker.Call(Request("logs"), 2s).status(), wire::OK);
    EXPECT_EQ(events, 128u);
    EXPECT_EQ(worker.DroppedEvents(), 172u);
}
TEST(ProcessWorkerTests, ExtensionFailureIsAResultNotTransportSuccess)
{
    WorkerSupervisor worker(Options());
    worker.Start();
    const auto response = worker.Call(Request("error"), 1s);
    EXPECT_EQ(response.status(), wire::EXTENSION_FAILURE);
    EXPECT_EQ(response.diagnostic(), "SIMULATED_EXTENSION_ERROR");
    EXPECT_EQ(worker.State(), WorkerState::Ready);
}
TEST(ProcessWorkerTests, WorkerDoesNotInheritUnrelatedEnvironmentSecrets)
{
    const auto previousSize = GetEnvironmentVariableW(L"ARTEST_TEST_SECRET", nullptr, 0);
    std::wstring previous(previousSize, L'\0');
    if (previousSize) GetEnvironmentVariableW(L"ARTEST_TEST_SECRET", previous.data(), previousSize);
    struct RestoreEnvironment
    {
        std::wstring value;
        ~RestoreEnvironment() { SetEnvironmentVariableW(L"ARTEST_TEST_SECRET", value.empty() ? nullptr : value.c_str()); }
    } restore{previous};
    SetEnvironmentVariableW(L"ARTEST_TEST_SECRET", L"not-a-real-secret");
    WorkerSupervisor worker(Options());
    worker.Start();
    const auto response = worker.Call(Request(), 1s);
    EXPECT_FALSE(nlohmann::json::parse(response.payload().json())["secretInherited"].get<bool>());
}
TEST(ProcessWorkerTests, InvalidHandshakeAndStartupAreContained)
{
    for (const auto mode : {L"bad-identity", L"bad-version", L"bad-nonce", L"no-connect"})
    {
        auto options = Options(mode);
        options.startupTimeout = 200ms;
        WorkerSupervisor worker(options);
        EXPECT_THROW(worker.Start(), ProcessError);
        EXPECT_EQ(worker.State(), WorkerState::Faulted);
        EXPECT_TRUE(Exited(worker.ProcessId()));
    }
}
namespace
{
class TestLease final : public artest::extensions::ComponentLease {};
class TestRuntime final : public artest::extensions::IExtensionRuntime
{
  public:
    bool failInitialize = false;
    std::vector<std::string> operations;
    nlohmann::json ValidateCatalog(const std::filesystem::path &) const override { return {}; }
    nlohmann::json CatalogSnapshot() const override { return {}; }
    artest::OperationResult Refresh(const std::filesystem::path &, artest::CommandRegistry &,
        artest::InstrumentRegistry &, const std::string &) override { return {}; }
    artest::ValueResult<std::shared_ptr<artest::extensions::ComponentLease>> CreateComponent(
        const std::string &, const nlohmann::json &) override
    {
        artest::ValueResult<std::shared_ptr<artest::extensions::ComponentLease>> result;
        result.value = std::make_shared<TestLease>();
        return result;
    }
    artest::OperationResult Invoke(const std::shared_ptr<artest::extensions::ComponentLease> &,
        const std::string &operation, const nlohmann::json &, const artest::CancellationToken *,
        artest::extensions::InvocationOutput *) override
    {
        operations.push_back(operation);
        return failInitialize && operation == "artest.lifecycle.initialize.v1"
            ? artest::OperationResult::Failure("TEST_INIT_FAILED", "simulated")
            : artest::OperationResult::Success();
    }
    artest::OperationResult RegisterService(std::string,
        const std::shared_ptr<artest::extensions::ComponentLease> &) override { return {}; }
    void UnregisterService(const std::string &) noexcept override {}
};
}
TEST(RuntimeSeamTests, NonNativeLeaseUsesExistingInstrumentLifecycle)
{
    auto runtime = std::make_shared<TestRuntime>();
    auto instrument = artest::extensions::MakeExtensionInstrument(runtime, "com.test.driver");
    instrument->SetId("DSO1");
    EXPECT_TRUE(instrument->Initialize(nlohmann::json::object()).Succeeded());
    EXPECT_TRUE(instrument->Shutdown().Succeeded());
    ASSERT_EQ(runtime->operations.size(), 2u);
    EXPECT_EQ(runtime->operations[1], "artest.lifecycle.shutdown.v1");
}
TEST(RuntimeSeamTests, PartialInitializationStillAttemptsCleanupThroughNeutralPort)
{
    auto runtime = std::make_shared<TestRuntime>();
    runtime->failInitialize = true;
    auto instrument = artest::extensions::MakeExtensionInstrument(runtime, "com.test.driver");
    EXPECT_FALSE(instrument->Initialize(nlohmann::json::object()).Succeeded());
    ASSERT_EQ(runtime->operations.size(), 2u);
    EXPECT_EQ(runtime->operations[1], "artest.lifecycle.shutdown.v1");
}

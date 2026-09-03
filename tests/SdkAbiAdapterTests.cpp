#include "TestSupport/SdkHost.h"
#include <gtest/gtest.h>

using namespace artest::sdk;
using sdk_tests::Harness;

namespace
{
struct Counters
{
    unsigned created = 0, executed = 0, initialized = 0, shutdown = 0;
} counters;
class TestCommand final : public Command
{
  public:
    TestCommand()
    {
        ++counters.created;
    }
    Result Execute(const Parameters &p, Context &context) override
    {
        ++counters.executed;
        const auto action = p.Optional<std::string>("action", "success");
        if (action == "throw")
            throw std::runtime_error("Command exception");
        if (action == "unknownThrow")
            throw 7;
        if (action == "fail")
            return Result::Failure(Status::ExtensionFailure, "Expected command failure.");
        if (action == "service")
            return context.CallInstrument("test.contract", "test/read", {{"value", 7}});
        if (action == "wait")
            return context.WaitFor(std::chrono::milliseconds{p.Get<int>("duration")});
        return Result::Success("Command completed.");
    }
};
class TestDriver final : public InstrumentDriver
{
  public:
    TestDriver()
    {
        ++counters.created;
        RegisterOperation("test/read", [](const Parameters &, Context &) {
            return Result::WithData({{"value", 42}});
        });
    }
    Result Initialize(const Parameters &p, Context &) override
    {
        ++counters.initialized;
        m_failShutdown = p.Optional<bool>("failShutdown", false);
        if (p.Optional<bool>("throwInitialize", false))
            throw std::runtime_error("Partial initialization exception");
        if (p.Optional<bool>("failInitialize", false))
            return Result::Failure(Status::ResourceUnavailable, "Partial initialization failure");
        return Result::Success();
    }
    Result Shutdown(Context &context) override
    {
        ++counters.shutdown;
        if (auto status = context.Checkpoint(); !status)
            return status;
        if (m_failShutdown)
            throw std::runtime_error("Shutdown failure");
        return Result::Success();
    }

  private:
    bool m_failShutdown = false;
};
class BadConstructor final : public Command
{
  public:
    BadConstructor()
    {
        throw std::runtime_error("Constructor failed");
    }
    Result Execute(const Parameters &, Context &) override
    {
        return Result::Success();
    }
};
Extension Define()
{
    Extension extension{"com.artest.test.sdk", "0.1.0"};
    extension.AddCommand<TestCommand>({.id = "test.command", .name = "Test command"});
    extension.AddCommand<BadConstructor>({.id = "test.bad-constructor", .name = "Bad constructor"});
    extension.AddDriver<TestDriver>({.id = "test.driver",
                                     .name = "Test driver",
                                     .contract = "test.contract",
                                     .mode = DriverMode::Simulated});
    return extension;
}
Extension DuplicateDefinition()
{
    Extension extension{"com.artest.test.duplicate", "0.1.0"};
    extension.AddCommand<TestCommand>({.id = "duplicate", .name = "First"});
    extension.AddDriver<TestDriver>(
        {.id = "duplicate", .name = "Second", .contract = "test.contract"});
    return extension;
}
Extension EmptyDefinition()
{
    return {"com.artest.test.empty", "0.1.0"};
}
using Runtime = Harness<Define>;
Json Request(const std::string &action = "success")
{
    return {{"parameters", {{"action", action}}}, {"instrumentId", "PS1"}};
}
} // namespace

TEST(SdkAbiMetadataTests, QueryDoesNotConstructCommandsOrDrivers)
{
    const auto before = counters.created;
    ARTestExtensionApiV0 api{};
    api.struct_size = sizeof(api);
    EXPECT_EQ(detail::NativeAdapter<Define>::Query(0, 1, &api, nullptr), ARTEST_STATUS_OK);
    EXPECT_EQ(counters.created, before);
    EXPECT_EQ(detail::Text(api.extension_id), "com.artest.test.sdk");
}
TEST(SdkAbiMetadataTests, DuplicateAndEmptyDefinitionsAreRejected)
{
    ARTestExtensionApiV0 api{};
    api.struct_size = sizeof(api);
    EXPECT_EQ(detail::NativeAdapter<DuplicateDefinition>::Query(0, 1, &api, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(detail::NativeAdapter<EmptyDefinition>::Query(0, 1, &api, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
}
TEST(SdkAbiMetadataTests, QueryPreservesBytesBeyondTheCurrentTable)
{
    struct Storage
    {
        ARTestExtensionApiV0 api{};
        std::uint64_t sentinel = 0x123456789abcdef0;
    } storage;
    storage.api.struct_size = sizeof(storage);
    EXPECT_EQ(detail::NativeAdapter<Define>::Query(0, 1, &storage.api, nullptr), ARTEST_STATUS_OK);
    EXPECT_EQ(storage.sentinel, UINT64_C(0x123456789abcdef0));
    storage.api.struct_size = sizeof(storage.api) - 1;
    EXPECT_EQ(detail::NativeAdapter<Define>::Query(0, 1, &storage.api, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
    storage.api.struct_size = sizeof(storage.api);
    EXPECT_EQ(detail::NativeAdapter<Define>::Query(1, 0, &storage.api, nullptr),
              ARTEST_STATUS_INCOMPATIBLE_ABI);
    EXPECT_EQ(detail::NativeAdapter<Define>::Query(0, 2, &storage.api, nullptr),
              ARTEST_STATUS_INCOMPATIBLE_ABI);
}
TEST(SdkAbiMetadataTests, DescriptorsHaveStableIdentityKindAndFlags)
{
    Runtime runtime;
    ASSERT_EQ(runtime.api.get_component_type_count(runtime.extension), 3U);
    ARTestComponentDescriptorV0 descriptor{};
    descriptor.struct_size = sizeof(descriptor);
    ASSERT_EQ(runtime.api.get_component_descriptor(runtime.extension, 2, &descriptor,
                                                   &runtime.error.value),
              ARTEST_STATUS_OK);
    EXPECT_EQ(detail::Text(descriptor.type_id), "test.driver");
    EXPECT_EQ(detail::Text(descriptor.contract_id), "test.contract");
    EXPECT_EQ(descriptor.kind, ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER);
    EXPECT_EQ(descriptor.flags, ARTEST_COMPONENT_FLAG_SIMULATED);
    EXPECT_EQ(runtime.api.get_component_descriptor(runtime.extension, 3, &descriptor, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
}
TEST(SdkAbiMetadataTests, InvalidHostTablesDoNotProduceHandles)
{
    Runtime runtime;
    auto host = runtime.host.Api();
    ARTestExtensionHandle extension = reinterpret_cast<ARTestExtensionHandle>(1);
    host.resolve_service = nullptr;
    EXPECT_EQ(runtime.api.create_extension(&host, nullptr, &extension, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(extension, nullptr);
    host = runtime.host.Api();
    host.struct_size = 4;
    EXPECT_EQ(runtime.api.create_extension(&host, nullptr, &extension, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(extension, nullptr);
}
TEST(SdkAbiLifetimeTests, ConstructorFailureAndUnknownTypeReturnNoHandle)
{
    Runtime runtime;
    ARTestComponentHandle component = reinterpret_cast<ARTestComponentHandle>(1);
    EXPECT_EQ(runtime.api.create_component(runtime.extension, detail::View("test.bad-constructor"),
                                           nullptr, &component, nullptr),
              ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_EQ(component, nullptr);
    component = reinterpret_cast<ARTestComponentHandle>(1);
    EXPECT_EQ(runtime.api.create_component(runtime.extension, detail::View("unknown"), nullptr,
                                           &component, nullptr),
              ARTEST_STATUS_NOT_FOUND);
    EXPECT_EQ(component, nullptr);
}
TEST(SdkAbiLifetimeTests, CrossExtensionComponentHandlesAreRejected)
{
    Runtime first, second;
    const auto component = first.Create("test.command");
    auto invocation = second.host.Invocation();
    EXPECT_EQ(second.api.invoke_component(second.extension, component,
                                          detail::View("artest.command.execute.v1"), nullptr,
                                          &invocation, nullptr, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
    second.api.destroy_component(second.extension, component); // Must not delete first's component.
    EXPECT_EQ(first.Invoke(component, "artest.command.execute.v1", Request()), ARTEST_STATUS_OK);
}
TEST(SdkAbiLifetimeTests, DriverRequiresInitializationAndCannotBeReusedAfterShutdown)
{
    Runtime runtime;
    const auto driver = runtime.Create("test.driver");
    EXPECT_EQ(runtime.Invoke(driver, "test/read"), ARTEST_STATUS_INVALID_STATE);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"),
              ARTEST_STATUS_INVALID_STATE);
    EXPECT_EQ(runtime.Invoke(driver, "test/read"), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.output["value"], 42);
    EXPECT_EQ(runtime.Invoke(driver, "unknown"), ARTEST_STATUS_OPERATION_NOT_SUPPORTED);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.Invoke(driver, "test/read"), ARTEST_STATUS_INVALID_STATE);
}
TEST(SdkAbiLifetimeTests, PartialInitializationCanCleanUpDespiteCancellationAndDeadline)
{
    Runtime runtime;
    const auto driver = runtime.Create("test.driver", {{"failInitialize", true}});
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"),
              ARTEST_STATUS_RESOURCE_UNAVAILABLE);
    runtime.host.cancelled = true;
    const auto before = counters.shutdown;
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1", Json::object(), 1),
              ARTEST_STATUS_OK);
    EXPECT_EQ(counters.shutdown, before + 1);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
    EXPECT_EQ(counters.shutdown, before + 1);
}
TEST(SdkAbiLifetimeTests, InitializationExceptionsStillAllowCleanup)
{
    Runtime runtime;
    const auto driver = runtime.Create("test.driver", {{"throwInitialize", true}});
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"),
              ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
}
TEST(SdkAbiLifetimeTests, ShutdownExceptionsAreReportedNotConvertedToSuccess)
{
    Runtime runtime;
    const auto driver = runtime.Create("test.driver", {{"failShutdown", true}});
    ASSERT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"),
              ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_NE(runtime.error.Message().find("Shutdown failure"), std::string::npos);
}
TEST(SdkAbiBoundaryTests, StandardAndUnknownExceptionsNeverEscape)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("throw")),
              ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_NE(runtime.error.Message().find("Command exception"), std::string::npos);
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("unknownThrow")),
              ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_NE(runtime.error.Message().find("Unhandled"), std::string::npos);
}
TEST(SdkAbiBoundaryTests, SmallErrorBuffersReportSizeWithoutTruncationOrReplay)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    runtime.error.value.capacity = 5;
    const auto before = counters.executed;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("fail")),
              ARTEST_STATUS_BUFFER_TOO_SMALL);
    EXPECT_EQ(runtime.error.value.required_size,
              std::string{"Expected command failure."}.size() + 1);
    EXPECT_EQ(runtime.error.text[0], '\0');
    EXPECT_EQ(counters.executed, before + 1);
}
TEST(SdkAbiBoundaryTests, SuccessClearsPreviousErrorAndWritesOnlyOneResult)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("fail")),
              ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request()), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.error.value.required_size, 0U);
    EXPECT_EQ(runtime.error.text[0], '\0');
    EXPECT_EQ(runtime.writes, 1U);
    EXPECT_EQ(runtime.output["message"], "Command completed.");
}
TEST(SdkAbiBoundaryTests, MalformedAndOversizedPayloadsAreRejectedBeforeConstruction)
{
    Runtime runtime;
    for (const auto &text :
         {std::string{"{bad"}, std::string(65, '[') + std::string(65, ']'), std::string{"[]"}})
    {
        auto payload = detail::Payload(text);
        ARTestComponentHandle component = nullptr;
        EXPECT_EQ(runtime.api.create_component(runtime.extension, detail::View("test.command"),
                                               &payload, &component, nullptr),
                  ARTEST_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(component, nullptr);
    }
    ARTestPayloadView payload{
        sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_JSON_UTF8, {}, {}, {nullptr, 42}};
    ARTestComponentHandle component = nullptr;
    EXPECT_EQ(runtime.api.create_component(runtime.extension, detail::View("test.command"),
                                           &payload, &component, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
    const std::string small = "{}";
    payload = detail::Payload(small);
    payload.bytes.size = 17 * 1024 * 1024;
    EXPECT_EQ(runtime.api.create_component(runtime.extension, detail::View("test.command"),
                                           &payload, &component, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
}
TEST(SdkAbiBoundaryTests, InvalidInvocationAndResultSinkAreRejected)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    auto context = runtime.host.Invocation();
    context.struct_size = 4;
    EXPECT_EQ(runtime.api.invoke_component(runtime.extension, command,
                                           detail::View("artest.command.execute.v1"), nullptr,
                                           &context, nullptr, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
    context = runtime.host.Invocation();
    ARTestResultSinkV0 sink{sizeof(ARTestResultSinkV0), 0, nullptr, nullptr};
    EXPECT_EQ(runtime.api.invoke_component(runtime.extension, command,
                                           detail::View("artest.command.execute.v1"), nullptr,
                                           &context, &sink, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(runtime.api.invoke_component(runtime.extension, command,
                                           detail::View("artest.command.execute.v1"), nullptr,
                                           nullptr, nullptr, nullptr),
              ARTEST_STATUS_INVALID_ARGUMENT);
}
TEST(SdkAbiBoundaryTests, InvalidErrorBufferIsNotWritten)
{
    Runtime runtime;
    struct Storage
    {
        ARTestErrorBuffer error{};
        std::uint64_t sentinel = 0xfeed;
    } storage;
    storage.error.struct_size = 4;
    EXPECT_EQ(detail::NativeAdapter<Define>::Query(0, 1, &runtime.api, &storage.error),
              ARTEST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(storage.sentinel, UINT64_C(0xfeed));
    EXPECT_EQ(storage.error.required_size, 0U);
}
TEST(SdkAbiBoundaryTests, UnknownOperationsAndMissingParametersAreNotSuccess)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    EXPECT_EQ(runtime.Invoke(command, "unknown", Request()), ARTEST_STATUS_OPERATION_NOT_SUPPORTED);
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Json::object()),
              ARTEST_STATUS_INVALID_ARGUMENT);
}
TEST(SdkAbiContextTests, CancellationAndDeadlinePreventCommandExecution)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    const auto before = counters.executed;
    runtime.host.cancelled = true;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request()),
              ARTEST_STATUS_CANCELLED);
    runtime.host.cancelled = false;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request(), 99),
              ARTEST_STATUS_TIMED_OUT);
    EXPECT_EQ(counters.executed, before);
}
TEST(SdkAbiContextTests, CooperativeWaitChecksCancellationDeadlineAndDuration)
{
    sdk_tests::Host host;
    auto api = host.Api();
    auto invocation = host.Invocation();
    invocation.deadline_monotonic_ns = 50;
    detail::NativeContext context{api, invocation, "test", {}};
    EXPECT_EQ(context.WaitFor(std::chrono::milliseconds{100}).Code(), Status::TimedOut);
    invocation.deadline_monotonic_ns = 0;
    host.cancelled = true;
    EXPECT_EQ(context.WaitFor(std::chrono::milliseconds{100}).Code(), Status::Cancelled);
    host.cancelled = false;
    EXPECT_EQ(context.WaitFor(std::chrono::milliseconds{-1}).Code(), Status::InvalidArgument);
    EXPECT_TRUE(context.WaitFor(std::chrono::milliseconds{1}));
}
TEST(SdkAbiContextTests, ServiceCallsForwardBindingContextAndReleaseOwnership)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("service")),
              ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.host.lastContract, "test.contract");
    EXPECT_EQ(runtime.host.lastInstance, "PS1");
    EXPECT_EQ(runtime.host.lastOperation, "test/read");
    EXPECT_EQ(runtime.host.lastRequest["value"], 7);
    EXPECT_EQ(runtime.host.lastInvocation.invocation_id, 42U);
    EXPECT_EQ(runtime.host.resolved, 1U);
    EXPECT_EQ(runtime.host.released, 1U);
    EXPECT_EQ(runtime.output["value"], 42);
}
TEST(SdkAbiContextTests, FailedServiceInvocationStillReleasesHandle)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    runtime.host.invokeStatus = ARTEST_STATUS_RESOURCE_UNAVAILABLE;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("service")),
              ARTEST_STATUS_RESOURCE_UNAVAILABLE);
    EXPECT_EQ(runtime.host.released, 1U);
}
TEST(SdkAbiContextTests, ReleaseFailuresCannotTurnIntoSuccessOrDoubleRelease)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    runtime.host.throwRelease = true;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("service")),
              ARTEST_STATUS_HOST_FAILURE);
    EXPECT_EQ(runtime.host.released, 1U);
    EXPECT_NE(runtime.error.Message().find("release"), std::string::npos);
}
TEST(SdkAbiContextTests, ServiceExceptionsAndHandlesReturnedWithErrorsAreContained)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    runtime.host.throwInvoke = true;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("service")),
              ARTEST_STATUS_HOST_FAILURE);
    EXPECT_EQ(runtime.host.released, 1U);
    runtime.host.throwInvoke = false;
    runtime.host.resolveStatus = ARTEST_STATUS_NOT_FOUND;
    runtime.host.returnHandleOnFailure = true;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("service")),
              ARTEST_STATUS_NOT_FOUND);
    EXPECT_EQ(runtime.host.released, 2U);
    runtime.host.throwResolve = true;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("service")),
              ARTEST_STATUS_HOST_FAILURE);
    EXPECT_EQ(runtime.host.released, 3U);
}
TEST(SdkAbiContextTests, MalformedOrRepeatedServiceResultsCannotReportSuccess)
{
    Runtime runtime;
    const auto command = runtime.Create("test.command");
    runtime.host.malformedResult = true;
    EXPECT_NE(runtime.Invoke(command, "artest.command.execute.v1", Request("service")),
              ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.host.released, 1U);
    runtime.host.malformedResult = false;
    runtime.host.doubleResult = true;
    EXPECT_EQ(runtime.Invoke(command, "artest.command.execute.v1", Request("service")),
              ARTEST_STATUS_HOST_FAILURE);
    EXPECT_EQ(runtime.host.released, 2U);
}

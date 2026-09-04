#include "ARTestDrvSimPower/SimPowerDriver.h"
#include "ARTestDrvSimCAN/SimCanDriver.h"
#include "ARTestCmdSample/PowerCycleCommand.h"
#include "ARTestCmdHardware/PowerOnCommand.h"
#include "ARTestCmdHardware/PowerOffCommand.h"
#include "ARTestCmdHardware/SendCanMessageCommand.h"
#include "TestSupport/SdkHost.h"
#include <ARTest/Testing.h>
#include <gtest/gtest.h>

using namespace artest::sdk;
using namespace artest::extensions;
using artest::sdk::testing::TestContext;

namespace
{
constexpr auto powerPrefix = "artest.instrument.power-supply.v1/";
const Json channel = {{"channel", 1}};
const Json power = {{"channel", 1}, {"voltage", 12.0}, {"currentLimit", 2.0}};
const Json frame = {{"channel", 0}, {"id", "0x123"}, {"dlc", 2}, {"data", {0, 255}}};

Extension DefineDrivers()
{
    Extension extension{"test.reference-drivers", "0.1.0"};
    extension.AddDriver<SimPowerDriver>({.id = "power", .name = "Power", .contract = "power"});
    extension.AddDriver<LegacySimPowerDriver>({.id = "legacy", .name = "Legacy", .contract = "power"});
    extension.AddDriver<SimCanDriver>({.id = "can", .name = "CAN", .contract = "can"});
    return extension;
}
using Runtime = sdk_tests::Harness<DefineDrivers>;
} // namespace

TEST(ReferenceDriverTests, PowerStateAndResponseSchemaArePreservedAndInstancesAreIndependent)
{
    SimPowerDriver first, second;
    TestContext context;
    const Json empty = Json::object();
    ASSERT_TRUE(first.Initialize(Parameters{empty}, context));
    ASSERT_TRUE(second.Initialize(Parameters{empty}, context));
    ASSERT_TRUE(first.Dispatch(std::string{powerPrefix} + "set-voltage", Parameters{power}, context));
    ASSERT_TRUE(first.Dispatch(std::string{powerPrefix} + "turn-on", Parameters{channel}, context));
    auto result = first.Dispatch(std::string{powerPrefix} + "read-state", Parameters{channel}, context);
    ASSERT_TRUE(result.Data());
    EXPECT_EQ(*result.Data(), (Json{{"channel", 1}, {"voltage", 12.0}, {"outputOn", true}}));
    EXPECT_EQ(result.SchemaId(), "artest.schema.instrument.power-supply.result.v1");
    result = second.Dispatch(std::string{powerPrefix} + "read-state", Parameters{channel}, context);
    ASSERT_TRUE(result.Data());
    EXPECT_EQ((*result.Data())["voltage"], 0.0);
    EXPECT_EQ((*result.Data())["outputOn"], false);
    EXPECT_TRUE(first.Shutdown(context));
    EXPECT_TRUE(second.Shutdown(context));
}

TEST(ReferenceDriverTests, LegacyTypeStillRequiresResourceButNativeTypeDoesNot)
{
    Runtime runtime;
    const auto native = runtime.Create("power");
    const auto legacy = runtime.Create("legacy");
    const auto configured = runtime.Create("legacy", {{"hw-rsrc", "SIM::PS1"}});
    EXPECT_EQ(runtime.Invoke(native, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.Invoke(legacy, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_RESOURCE_UNAVAILABLE);
    EXPECT_NE(runtime.error.Message().find("POWER_SUPPLY_RESOURCE_MISSING"), std::string::npos);
    EXPECT_EQ(runtime.Invoke(configured, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_OK);
    for (const auto component : {native, legacy, configured})
        EXPECT_EQ(runtime.Invoke(component, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
}

TEST(ReferenceDriverTests, PowerRetryCounterAndTurnOffPreserveBehavior)
{
    Runtime runtime;
    const auto driver = runtime.Create("power", {{"failTurnOnAttempts", 1}});
    ASSERT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.Invoke(driver, std::string{powerPrefix} + "turn-on", channel), ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_NE(runtime.error.Message().find("POWER_SUPPLY_TURN_ON_SIMULATED_FAILURE"), std::string::npos);
    EXPECT_EQ(runtime.Invoke(driver, std::string{powerPrefix} + "turn-on", channel), ARTEST_STATUS_OK);
    ASSERT_EQ(runtime.Invoke(driver, std::string{powerPrefix} + "read-state", channel), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.output["outputOn"], true);
    EXPECT_EQ(runtime.schemaId, "artest.schema.instrument.power-supply.result.v1");
    EXPECT_EQ(runtime.Invoke(driver, std::string{powerPrefix} + "turn-off", channel), ARTEST_STATUS_OK);
    ASSERT_EQ(runtime.Invoke(driver, std::string{powerPrefix} + "read-state", channel), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.output["outputOn"], false);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
}

TEST(ReferenceDriverTests, PowerRejectsInvalidValuesWithoutMutation)
{
    Runtime runtime;
    const auto driver = runtime.Create("power");
    ASSERT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_OK);
    for (const auto &input : std::vector<Json>{
             {{"channel", -1}, {"voltage", 12}}, {{"channel", 1}, {"voltage", -1}},
             {{"channel", 1}, {"voltage", "12"}}, {{"channel", 1.0}, {"voltage", 12}}})
        EXPECT_EQ(runtime.Invoke(driver, std::string{powerPrefix} + "set-voltage", input),
                  ARTEST_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(runtime.Invoke(driver, std::string{powerPrefix} + "set-current-limit",
                             {{"channel", 1}, {"currentLimit", -1}}), ARTEST_STATUS_INVALID_ARGUMENT);
    ASSERT_EQ(runtime.Invoke(driver, std::string{powerPrefix} + "read-state", channel), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.output["voltage"], 0.0);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
}

TEST(ReferenceDriverTests, PartialInitializationAndCancelledCleanupKeepBothFailures)
{
    for (const auto flag : {"failInitialize", "failInitialization"})
    {
        Runtime runtime;
        const auto driver = runtime.Create("power", {{flag, true}, {"failShutdown", true}});
        EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_RESOURCE_UNAVAILABLE);
        runtime.host.cancelled = true;
        EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1", Json::object(), 1),
                  ARTEST_STATUS_EXTENSION_FAILURE);
        EXPECT_EQ(runtime.error.Message(), "Simulated shutdown failure was requested.");
        EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
        EXPECT_EQ(std::count(runtime.host.logs.begin(), runtime.host.logs.end(),
                             "Simulated power driver shut down."), 1);
    }
}

TEST(ReferenceDriverTests, CanLifecycleAndFrameCountsArePreserved)
{
    Runtime runtime;
    const auto driver = runtime.Create("can", {{"hw-rsrc", "SIM::CAN1"}});
    EXPECT_EQ(runtime.Invoke(driver, "artest.instrument.can.v1/send", frame), ARTEST_STATUS_INVALID_STATE);
    ASSERT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_OK);
    for (const auto id : {"0x123", "291", "0443", "0x1fffffff"})
    {
        auto input = frame;
        input["id"] = id;
        ASSERT_EQ(runtime.Invoke(driver, "artest.instrument.can.v1/send", input), ARTEST_STATUS_OK);
        EXPECT_EQ(runtime.output["sent"], true);
    }
    EXPECT_EQ(runtime.output["messageCount"], 4);
    EXPECT_EQ(runtime.Invoke(driver, "unknown"), ARTEST_STATUS_OPERATION_NOT_SUPPORTED);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.Invoke(driver, "artest.instrument.can.v1/send", frame), ARTEST_STATUS_INVALID_STATE);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_INVALID_STATE);
}

TEST(ReferenceDriverTests, CanRejectsMalformedFramesWithoutIncrementingCount)
{
    Runtime runtime;
    const auto driver = runtime.Create("can", {{"hw-rsrc", "SIM::CAN1"}});
    ASSERT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_OK);
    std::vector<Json> invalid;
    for (const auto id : {"junk", "0x20000000", "4294967296", "123tail"})
    {
        auto input = frame;
        input["id"] = id;
        invalid.push_back(input);
    }
    for (const auto data : {Json::array({1}), Json::array({1, 256}), Json::array({-1, 0}),
                            Json::array({1.0, 0}), Json::array({true, 0}), Json::object()})
    {
        auto input = frame;
        input["data"] = data;
        invalid.push_back(input);
    }
    auto missing = frame;
    missing.erase("id");
    invalid.push_back(missing);
    for (const auto &input : invalid)
        EXPECT_EQ(runtime.Invoke(driver, "artest.instrument.can.v1/send", input), ARTEST_STATUS_INVALID_ARGUMENT)
            << input.dump();
    ASSERT_EQ(runtime.Invoke(driver, "artest.instrument.can.v1/send", frame), ARTEST_STATUS_OK);
    EXPECT_EQ(runtime.output["messageCount"], 1);
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1"), ARTEST_STATUS_OK);
}

TEST(ReferenceDriverTests, CanPartialInitializationStillReportsShutdownFailure)
{
    Runtime runtime;
    const auto driver = runtime.Create("can", {{"failShutdown", true}});
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.initialize.v1"), ARTEST_STATUS_RESOURCE_UNAVAILABLE);
    EXPECT_NE(runtime.error.Message().find("CAN_RESOURCE_MISSING"), std::string::npos);
    runtime.host.cancelled = true;
    EXPECT_EQ(runtime.Invoke(driver, "artest.lifecycle.shutdown.v1", Json::object(), 1),
              ARTEST_STATUS_EXTENSION_FAILURE);
    EXPECT_EQ(runtime.error.Message(), "Simulated CAN cleanup failure.");
}

TEST(ReferenceCommandTests, PowerOnPreservesCallOrderBindingAndMessage)
{
    PowerOnCommand command;
    TestContext context;
    context.instrumentId = "PS1";
    context.onCall = [](const auto &) { return Result::Success(); };
    const auto result = command.Execute(Parameters{power}, context);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.Message(), "Power On completed.");
    ASSERT_EQ(context.calls.size(), 3U);
    for (const auto &call : context.calls)
    {
        EXPECT_EQ(call.contract, "artest.contract.instrument.power-supply.v1");
        EXPECT_EQ(call.instance, "PS1");
        EXPECT_EQ(call.request, power);
    }
    EXPECT_EQ(context.calls[0].operation, std::string{powerPrefix} + "set-voltage");
    EXPECT_EQ(context.calls[1].operation, std::string{powerPrefix} + "set-current-limit");
    EXPECT_EQ(context.calls[2].operation, std::string{powerPrefix} + "turn-on");
}

TEST(ReferenceCommandTests, PowerOnStopsAtEachFailedServiceCall)
{
    for (std::size_t failAt = 1; failAt <= 3; ++failAt)
    {
        PowerOnCommand command;
        TestContext context;
        context.instrumentId = "PS1";
        context.onCall = [&](const auto &) {
            return context.calls.size() == failAt
                ? Result::Failure(Status::HostFailure, "Service failure") : Result::Success();
        };
        const auto result = command.Execute(Parameters{power}, context);
        EXPECT_EQ(result.Code(), Status::HostFailure);
        EXPECT_EQ(result.Message(), "Service failure");
        EXPECT_EQ(context.calls.size(), failAt);
    }
}

TEST(ReferenceCommandTests, PowerOffAndCanForwardInputsAndPropagateFailures)
{
    PowerOffCommand off;
    SendCanMessageCommand can;
    for (const bool fail : {false, true})
    {
        TestContext context;
        context.instrumentId = "device";
        context.onCall = [fail](const auto &) {
            return fail ? Result::Failure(Status::ResourceUnavailable, "Offline") : Result::Success();
        };
        const auto powerResult = off.Execute(Parameters{channel}, context);
        const auto canResult = can.Execute(Parameters{frame}, context);
        EXPECT_EQ(powerResult.Code(), fail ? Status::ResourceUnavailable : Status::Ok);
        EXPECT_EQ(canResult.Code(), fail ? Status::ResourceUnavailable : Status::Ok);
        ASSERT_EQ(context.calls.size(), 2U);
        EXPECT_EQ(context.calls[0].request, channel);
        EXPECT_EQ(context.calls[1].request, frame);
        EXPECT_EQ(context.calls[1].contract, "artest.contract.instrument.can.v1");
        EXPECT_EQ(context.calls[1].operation, "artest.instrument.can.v1/send");
        if (!fail)
        {
            EXPECT_EQ(powerResult.Message(), "Power Off completed.");
            EXPECT_EQ(canResult.Message(), "Send CAN Message completed.");
        }
    }
}

TEST(ReferenceCommandTests, InvalidInputAndMissingBindingCannotTriggerHardwareCalls)
{
    PowerOnCommand command;
    TestContext context;
    EXPECT_EQ(command.Execute(Parameters{power}, context).Code(), Status::InvalidArgument);
    auto invalid = power;
    invalid["voltage"] = -1;
    EXPECT_EQ(command.Execute(Parameters{invalid}, context).Code(), Status::InvalidArgument);
    invalid["voltage"] = "12";
    EXPECT_THROW((void)command.Execute(Parameters{invalid}, context), std::invalid_argument);
    EXPECT_TRUE(context.calls.empty());
}

TEST(ReferenceCommandTests, PowerCycleUsesLogicalTimeAndPreservesItsResult)
{
    PowerCycleCommand command;
    TestContext context;
    context.instrumentId = "PS1";
    context.onCall = [](const auto &) { return Result::Success(); };
    const Json parameters = {{"channel", 1}, {"voltage", 12}, {"holdMs", 25}};
    const auto result = command.Execute(Parameters{parameters}, context);
    ASSERT_TRUE(result.Data());
    EXPECT_EQ(*result.Data(), (Json{{"message", "Native command invoked the simulated driver successfully."},
                                  {"instrumentId", "PS1"}, {"channel", 1}}));
    ASSERT_EQ(context.calls.size(), 3U);
    EXPECT_EQ(context.calls[0].operation, std::string{powerPrefix} + "set-voltage");
    EXPECT_EQ(context.calls[1].operation, std::string{powerPrefix} + "turn-on");
    EXPECT_EQ(context.calls[2].operation, std::string{powerPrefix} + "turn-off");
    EXPECT_EQ(context.elapsed.count(), 25);
}

TEST(ReferenceCommandTests, PowerCyclePropagatesEveryServiceFailureIncludingTurnOff)
{
    const Json parameters = {{"channel", 1}, {"voltage", 12}};
    for (std::size_t failAt = 1; failAt <= 3; ++failAt)
    {
        PowerCycleCommand command;
        TestContext context;
        context.instrumentId = "PS1";
        context.onCall = [&](const auto &) {
            return context.calls.size() == failAt
                ? Result::Failure(Status::ExtensionFailure, "Device failure") : Result::Success();
        };
        EXPECT_EQ(command.Execute(Parameters{parameters}, context).Code(), Status::ExtensionFailure);
        EXPECT_EQ(context.calls.size(), failAt);
        EXPECT_TRUE(context.logs.empty());
    }
}

TEST(ReferenceCommandTests, PowerCycleCancellationAndDeadlineDoNotBecomeSuccess)
{
    const Json parameters = {{"channel", 1}, {"voltage", 12}, {"holdMs", 100}};
    for (const bool cancelDuringExecution : {false, true})
    {
        PowerCycleCommand command;
        TestContext context;
        context.instrumentId = "PS1";
        context.onCall = [&](const auto &) {
            if (cancelDuringExecution && context.calls.size() == 2)
                context.cancelled = true;
            return Result::Success();
        };
        if (!cancelDuringExecution)
            context.deadline = std::chrono::milliseconds{5};
        EXPECT_EQ(command.Execute(Parameters{parameters}, context).Code(),
                  cancelDuringExecution ? Status::Cancelled : Status::TimedOut);
        EXPECT_EQ(context.calls.size(), 2U);
        EXPECT_TRUE(context.logs.empty());
    }
}

TEST(ReferenceCommandTests, AlreadyCancelledCommandsMakeNoServiceCalls)
{
    PowerCycleCommand cycle;
    PowerOnCommand on;
    PowerOffCommand off;
    SendCanMessageCommand can;
    TestContext context;
    context.cancelled = true;
    context.instrumentId = "device";
    EXPECT_EQ(cycle.Execute(Parameters{power}, context).Code(), Status::Cancelled);
    EXPECT_EQ(on.Execute(Parameters{power}, context).Code(), Status::Cancelled);
    EXPECT_EQ(off.Execute(Parameters{channel}, context).Code(), Status::Cancelled);
    EXPECT_EQ(can.Execute(Parameters{frame}, context).Code(), Status::Cancelled);
    EXPECT_TRUE(context.calls.empty());
}

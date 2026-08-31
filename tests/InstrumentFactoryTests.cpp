#include "TestSupport/RecordingEventSink.h"

#include "ARTestEngine.Core/Instruments/Fakes/FakeCanDevice.h"
#include "ARTestEngine.Core/Instruments/Fakes/FakePowerSupply.h"
#include "ARTestEngine.Core/Instruments/Fakes/RegisterFakeInstruments.h"
#include "ARTestEngine.Core/Instruments/InstrumentManager.h"

#include <gtest/gtest.h>

using namespace artest;

namespace
{
    std::vector<InstrumentDefinition> ValidDefinitions()
    {
        return {
            {"PowerSupply", "PS1", {{"hw-rsrc", "FAKE::PS"}}},
            {"CAN", "CAN1", {{"hw-rsrc", "FAKE::CAN"}}}};
    }
}

TEST(InstrumentManagerTests, LoadingDefinitionsDoesNotInitializeHardware)
{
    RecordingEventSink sink;
    InstrumentRegistry registry;
    ASSERT_TRUE(RegisterFakeInstruments(registry).Succeeded());
    InstrumentManager manager(registry, sink);

    ASSERT_TRUE(manager.LoadDefinitions(ValidDefinitions()).Succeeded());
    const auto power = std::dynamic_pointer_cast<FakePowerSupply>(manager.GetInstrument("PS1"));

    ASSERT_NE(power, nullptr);
    EXPECT_FALSE(power->IsInitialized());
    EXPECT_TRUE(sink.events.empty());
}

TEST(InstrumentManagerTests, InitializeAndShutdownAreExplicitAndObservable)
{
    RecordingEventSink sink;
    InstrumentRegistry registry;
    ASSERT_TRUE(RegisterFakeInstruments(registry).Succeeded());
    InstrumentManager manager(registry, sink);
    ASSERT_TRUE(manager.LoadDefinitions(ValidDefinitions()).Succeeded());

    ASSERT_TRUE(manager.InitializeAll().Succeeded());
    auto power = std::dynamic_pointer_cast<FakePowerSupply>(manager.GetInstrument("PS1"));
    auto can = std::dynamic_pointer_cast<FakeCanDevice>(manager.GetInstrument("CAN1"));
    ASSERT_NE(power, nullptr);
    ASSERT_NE(can, nullptr);
    EXPECT_TRUE(power->IsInitialized());
    EXPECT_TRUE(can->IsInitialized());
    EXPECT_EQ(sink.Count(EngineEventKind::InstrumentInitialized), 2U);

    manager.ShutdownAll();
    EXPECT_FALSE(power->IsInitialized());
    EXPECT_FALSE(can->IsInitialized());
    EXPECT_EQ(sink.Count(EngineEventKind::InstrumentShutdown), 2U);
}

TEST(InstrumentManagerTests, RejectsUnknownTypesAndDuplicateIdentifiersAtomically)
{
    RecordingEventSink sink;
    InstrumentRegistry registry;
    ASSERT_TRUE(RegisterFakeInstruments(registry).Succeeded());
    InstrumentManager manager(registry, sink);
    const std::vector<InstrumentDefinition> definitions{
        {"Unknown", "unknown", nlohmann::json::object()},
        {"CAN", "duplicate", {{"hw-rsrc", "A"}}},
        {"CAN", "duplicate", {{"hw-rsrc", "B"}}}};

    const auto result = manager.LoadDefinitions(definitions);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.size(), 2U);
    EXPECT_EQ(manager.GetInstrument("duplicate"), nullptr);
}

TEST(InstrumentManagerTests, InitializationFailureIsReportedAndPreviouslyInitializedInstrumentsAreCleanedUp)
{
    RecordingEventSink sink;
    InstrumentRegistry registry;
    ASSERT_TRUE(RegisterFakeInstruments(registry).Succeeded());
    InstrumentManager manager(registry, sink);
    const std::vector<InstrumentDefinition> definitions{
        {"CAN", "A_CAN", {{"hw-rsrc", "FAKE::CAN"}}},
        {"PowerSupply", "B_PS", {{"hw-rsrc", "FAKE::PS"}, {"failInitialization", true}}}};
    ASSERT_TRUE(manager.LoadDefinitions(definitions).Succeeded());

    const auto result = manager.InitializeAll();

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "POWER_SUPPLY_INITIALIZATION_FORCED_FAILURE");
    const auto can = std::dynamic_pointer_cast<FakeCanDevice>(manager.GetInstrument("A_CAN"));
    ASSERT_NE(can, nullptr);
    EXPECT_FALSE(can->IsInitialized());
    EXPECT_EQ(sink.Count(EngineEventKind::InstrumentShutdown), 1U);
}

TEST(FakeInstrumentTests, RecordsPowerAndCanOperationsWithoutHardware)
{
    RecordingEventSink sink;
    FakePowerSupply power(sink);
    power.SetId("PS");
    ASSERT_TRUE(power.Initialize({{"hw-rsrc", "FAKE"}}).Succeeded());
    ASSERT_TRUE(power.SetVoltage(2, 13.5).Succeeded());
    ASSERT_TRUE(power.SetCurrent(2, 1.25).Succeeded());
    ASSERT_TRUE(power.TurnOn(2).Succeeded());
    EXPECT_TRUE(power.IsChannelOn(2));
    EXPECT_DOUBLE_EQ(power.Voltage(2), 13.5);
    EXPECT_DOUBLE_EQ(power.CurrentLimit(2), 1.25);

    FakeCanDevice can(sink);
    can.SetId("CAN");
    ASSERT_TRUE(can.Initialize({{"hw-rsrc", "FAKE"}}).Succeeded());
    ASSERT_TRUE(can.SendMessage(0, 0x123U, {1U, 2U}).Succeeded());
    ASSERT_EQ(can.SentMessages().size(), 1U);
    EXPECT_EQ(can.SentMessages().front().messageId, 0x123U);
}

#include "TestSupport/RecordingEventSink.h"

#include "ARTestEngine.Core/Commands/BuiltIn/RegisterBuiltInCommands.h"
#include "ARTestEngine.Core/Compilation/TestPlanCompiler.h"
#include "ARTestEngine.Core/Instruments/Fakes/FakePowerSupply.h"
#include "ARTestEngine.Core/Instruments/Fakes/RegisterFakeInstruments.h"

#include <gtest/gtest.h>

using namespace artest;

namespace
{
    StepDefinition Wait(std::uint64_t stepId, int milliseconds = 0)
    {
        return {stepId, "Time.WaitMs", std::nullopt, {{"milliseconds", milliseconds}}};
    }

    struct CompilerEnvironment
    {
        CompilerEnvironment()
            : instruments(instrumentRegistry, sink), compiler(commands, instruments)
        {
            EXPECT_TRUE(RegisterBuiltInCommands(commands).Succeeded());
            EXPECT_TRUE(RegisterFakeInstruments(instrumentRegistry).Succeeded());
        }

        RecordingEventSink sink;
        CommandRegistry commands;
        InstrumentRegistry instrumentRegistry;
        InstrumentManager instruments;
        TestPlanCompiler compiler;
    };
}

TEST(TestPlanCompilerTests, CompilesValidTypedDefinitionsWithoutExecutingOrInitializing)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.instruments.push_back({"PowerSupply", "PS1", {{"hw-rsrc", "FAKE"}}});
    plan.steps = {
        {1, "PowerSupply.TurnOn", "PS1", {{"channel", 1}, {"voltage", 12.0}, {"currentLimit", 2.0}}},
        Wait(2)};
    ASSERT_TRUE(environment.instruments.LoadDefinitions(plan.instruments).Succeeded());

    const auto result = environment.compiler.Compile(plan);

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.value->size(), 2U);
    const auto power = std::dynamic_pointer_cast<FakePowerSupply>(environment.instruments.GetInstrument("PS1"));
    ASSERT_NE(power, nullptr);
    EXPECT_FALSE(power->IsInitialized());
}

TEST(TestPlanCompilerTests, UnknownCommandInvalidatesTheWholeCompilation)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.steps = {Wait(1), {2, "Unknown.Command", std::nullopt, nlohmann::json::object()}};
    ASSERT_TRUE(environment.instruments.LoadDefinitions({}).Succeeded());

    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_FALSE(result.value.has_value());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_TYPE_UNKNOWN");
}

TEST(TestPlanCompilerTests, RejectsUnknownInstrument)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.steps = {{1, "PowerSupply.TurnOn", "missing", {{"channel", 1}, {"voltage", 12.0}, {"currentLimit", 2.0}}}};
    ASSERT_TRUE(environment.instruments.LoadDefinitions({}).Succeeded());

    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_INSTRUMENT_UNKNOWN");
}

TEST(TestPlanCompilerTests, RejectsDuplicateStepIdentifiers)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.steps = {Wait(1), Wait(1)};
    ASSERT_TRUE(environment.instruments.LoadDefinitions({}).Succeeded());

    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_STEP_ID_INVALID");
}

TEST(TestPlanCompilerTests, MissingParametersFailDuringOfflineCompilation)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.steps = {{1, "Time.WaitMs", std::nullopt, nlohmann::json::object()}};
    ASSERT_TRUE(environment.instruments.LoadDefinitions({}).Succeeded());

    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "WAIT_DURATION_INVALID");
    EXPECT_EQ(result.diagnostics.front().location, "stepId=1");
}

TEST(TestPlanCompilerTests, IfRemainsReservedAndFailsCompilationExplicitly)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.steps = {{1, "IF", std::nullopt, {{"condition", "x == 1"}}}};
    ASSERT_TRUE(environment.instruments.LoadDefinitions({}).Succeeded());

    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "IF_NOT_IMPLEMENTED");
}

TEST(RegistryTests, DuplicateRegistrationsAreRejected)
{
    CommandRegistry commands;
    ASSERT_TRUE(RegisterBuiltInCommands(commands).Succeeded());
    const auto duplicate = RegisterBuiltInCommands(commands);

    EXPECT_FALSE(duplicate.Succeeded());
    EXPECT_EQ(duplicate.diagnostics.size(), 5U);
}

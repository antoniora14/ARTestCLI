#include "TestSupport/RecordingEventSink.h"

#include "ARTestEngine.Core/Commands/BuiltIn/IntrinsicCommands.h"
#include "ARTestEngine.Core/Compilation/TestPlanCompiler.h"

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
            : compiler(catalog)
        {
            EXPECT_TRUE(RegisterIntrinsicMetadata(catalog).Succeeded());
            ComponentDescriptor power;
            power.kind = ComponentKind::InstrumentDriver;
            power.typeId = "PowerSupply";
            power.contractId = "artest.contract.instrument.power-supply.v1";
            power.schemas.push_back({"configuration", "artest.schema.test.configuration.v1",
                {}, {}, {{"type", "object"}}});
            ComponentDescriptor on;
            on.typeId = "PowerSupply.TurnOn";
            on.requiredContracts = {power.contractId};
            on.schemas.push_back({"parameters", "artest.schema.test.power.v1", {}, {}, {
                {"type", "object"}, {"required", {"channel", "voltage", "currentLimit"}},
                {"properties", {
                    {"channel", {{"type", "integer"}, {"minimum", 0}}},
                    {"voltage", {{"type", "number"}, {"minimum", 0}}},
                    {"currentLimit", {{"type", "number"}, {"minimum", 0}}}}}}});
            EXPECT_TRUE(catalog.Add({power, on}).Succeeded());
        }

        RecordingEventSink sink;
        ComponentCatalog catalog;
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

    const auto result = environment.compiler.Compile(plan);

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.value->size(), 2U);
    EXPECT_EQ(result.value->front().componentType, "PowerSupply.TurnOn");
    EXPECT_EQ(result.value->front().instrumentId, "PS1");
    const auto copy = *result.value;
    EXPECT_EQ(copy.front().parameters, plan.steps.front().parameters);
}

TEST(TestPlanCompilerTests, UnknownCommandInvalidatesTheWholeCompilation)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.steps = {Wait(1), {2, "Unknown.Command", std::nullopt, nlohmann::json::object()}};


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


    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_INSTRUMENT_UNKNOWN");
}

TEST(TestPlanCompilerTests, RejectsDuplicateStepIdentifiers)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.steps = {Wait(1), Wait(1)};


    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_STEP_ID_INVALID");
}

TEST(TestPlanCompilerTests, MissingParametersFailDuringOfflineCompilation)
{
    CompilerEnvironment environment;
    TestPlan plan;
    plan.steps = {{1, "Time.WaitMs", std::nullopt, nlohmann::json::object()}};


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


    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "IF_NOT_IMPLEMENTED");
}

TEST(RegistryTests, DuplicateRegistrationsAreRejected)
{
    CommandRegistry commands;
    ASSERT_TRUE(RegisterIntrinsicCommands(commands).Succeeded());
    const auto duplicate = RegisterIntrinsicCommands(commands);

    EXPECT_FALSE(duplicate.Succeeded());
    EXPECT_EQ(duplicate.diagnostics.size(), 1U);
}

TEST(TestPlanCompilerTests, RejectsUnsafeExecutionPolicyValues)
{
    CompilerEnvironment environment;
    TestPlan plan;
    auto definition = Wait(1);
    definition.policy.maxAttempts = 0;
    definition.policy.retryDelay = std::chrono::milliseconds{-1};
    plan.steps.push_back(std::move(definition));


    const auto result = environment.compiler.Compile(plan);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_POLICY_ATTEMPTS_INVALID");
}

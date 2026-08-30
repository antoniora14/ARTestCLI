#include "ArtCore/ScriptExecutor.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    class TestCommand final : public ICommand
    {
    public:
        TestCommand(std::string name, StepResult result, int& executionCount, bool valid = true)
            : m_name(std::move(name)), m_result(std::move(result)), m_executionCount(executionCount), m_valid(valid)
        {
        }

        std::string Name() const override { return m_name; }
        StepResult Execute(ExecutionContext&) override
        {
            ++m_executionCount;
            return m_result;
        }
        void Configure(const nlohmann::json&, std::shared_ptr<IInstrument>) override {}
        bool Validate(std::string& error) const override
        {
            if (!m_valid)
            {
                error = "Requested validation failure.";
            }
            return m_valid;
        }

    private:
        std::string m_name;
        StepResult m_result;
        int& m_executionCount;
        bool m_valid;
    };
}

TEST(ScriptExecutorTests, CompilationCollectsCommandDiagnosticsWithoutExecuting)
{
    int executions = 0;
    std::vector<CommandInstance> commands;
    commands.push_back({10, std::make_unique<TestCommand>("invalid", StepResult::Pass(), executions, false)});
    CScriptExecutor executor{std::move(commands)};

    const OperationResult result = executor.Compile();

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(executions, 0);
    EXPECT_EQ(result.diagnostics.front().location, "stepId=10");
}

TEST(ScriptExecutorTests, ExecutionStopsAtTheFirstFailedStep)
{
    int firstExecutions = 0;
    int secondExecutions = 0;
    std::vector<CommandInstance> commands;
    commands.push_back({1, std::make_unique<TestCommand>("fail", StepResult::Fail("expected"), firstExecutions)});
    commands.push_back({2, std::make_unique<TestCommand>("must-not-run", StepResult::Pass(), secondExecutions)});
    CScriptExecutor executor{std::move(commands)};

    const RunResult result = executor.Execute();

    EXPECT_EQ(result.status, RunStatus::Failed);
    EXPECT_EQ(firstExecutions, 1);
    EXPECT_EQ(secondExecutions, 0);
    ASSERT_EQ(result.steps.size(), 1U);
    EXPECT_EQ(result.steps.front().stepId, 1U);
}

TEST(ScriptExecutorTests, SuccessfulRunReturnsStepRecords)
{
    int firstExecutions = 0;
    int secondExecutions = 0;
    std::vector<CommandInstance> commands;
    commands.push_back({5, std::make_unique<TestCommand>("first", StepResult::Pass(), firstExecutions)});
    commands.push_back({7, std::make_unique<TestCommand>("second", StepResult::Pass(), secondExecutions)});
    CScriptExecutor executor{std::move(commands)};

    const RunResult result = executor.Execute();

    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.steps.size(), 2U);
    EXPECT_EQ(firstExecutions, 1);
    EXPECT_EQ(secondExecutions, 1);
}

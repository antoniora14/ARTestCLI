#include "TestSupport/RecordingEventSink.h"

#include "ARTestEngine.Core/Commands/ICommand.h"
#include "ARTestEngine.Core/Execution/TestExecutor.h"

#include <gtest/gtest.h>

using namespace artest;

namespace
{
    class TestCommand final : public ICommand
    {
    public:
        TestCommand(std::string name, StepResult result, int& executionCount, bool throws = false)
            : m_name(std::move(name)),
              m_result(std::move(result)),
              m_executionCount(executionCount),
              m_throws(throws)
        {
        }

        std::string Name() const override { return m_name; }
        OperationResult Configure(const nlohmann::json&, std::shared_ptr<IInstrument>) override
        {
            return OperationResult::Success();
        }
        OperationResult Validate() const override { return OperationResult::Success(); }
        StepResult Execute(ExecutionContext&) override
        {
            ++m_executionCount;
            if (m_throws)
            {
                throw std::runtime_error("expected exception");
            }
            return m_result;
        }

    private:
        std::string m_name;
        StepResult m_result;
        int& m_executionCount;
        bool m_throws;
    };

    class CancelAtIndex final : public IExecutionControl
    {
    public:
        explicit CancelAtIndex(std::size_t index) : m_index(index) {}
        ExecutionDecision BeforeStep(const StepExecutionInfo& step) override
        {
            return step.commandIndex == m_index ? ExecutionDecision::Cancel : ExecutionDecision::Continue;
        }

    private:
        std::size_t m_index;
    };
}

TEST(TestExecutorTests, ExecutionStopsAtTheFirstFailedStep)
{
    int firstExecutions = 0;
    int secondExecutions = 0;
    std::vector<CompiledStep> steps;
    steps.push_back({1, "fail", std::make_unique<TestCommand>("fail", StepResult::Fail("expected"), firstExecutions)});
    steps.push_back({2, "must-not-run", std::make_unique<TestCommand>("must-not-run", StepResult::Pass(), secondExecutions)});
    RecordingEventSink sink;

    const auto result = TestExecutor{sink}.Execute(steps);

    EXPECT_EQ(result.status, RunStatus::Failed);
    EXPECT_EQ(firstExecutions, 1);
    EXPECT_EQ(secondExecutions, 0);
    ASSERT_EQ(result.steps.size(), 1U);
    EXPECT_EQ(result.steps.front().stepId, 1U);
}

TEST(TestExecutorTests, SuccessfulRunReturnsRecordsAndTypedEvents)
{
    int firstExecutions = 0;
    int secondExecutions = 0;
    std::vector<CompiledStep> steps;
    steps.push_back({5, "first", std::make_unique<TestCommand>("first", StepResult::Pass(), firstExecutions)});
    steps.push_back({7, "second", std::make_unique<TestCommand>("second", StepResult::Pass(), secondExecutions)});
    RecordingEventSink sink;

    const auto result = TestExecutor{sink}.Execute(steps);

    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.steps.size(), 2U);
    EXPECT_EQ(sink.Count(EngineEventKind::StepStarted), 2U);
    EXPECT_EQ(sink.Count(EngineEventKind::StepCompleted), 2U);
    EXPECT_EQ(sink.Count(EngineEventKind::RunCompleted), 1U);
}

TEST(TestExecutorTests, CancellationOccursBeforeTheSelectedStep)
{
    int firstExecutions = 0;
    int secondExecutions = 0;
    std::vector<CompiledStep> steps;
    steps.push_back({1, "first", std::make_unique<TestCommand>("first", StepResult::Pass(), firstExecutions)});
    steps.push_back({2, "second", std::make_unique<TestCommand>("second", StepResult::Pass(), secondExecutions)});
    RecordingEventSink sink;
    ExecutionContext context;
    CancelAtIndex control(1);

    const auto result = TestExecutor{sink}.Execute(steps, context, control);

    EXPECT_EQ(result.status, RunStatus::Cancelled);
    EXPECT_EQ(firstExecutions, 1);
    EXPECT_EQ(secondExecutions, 0);
    EXPECT_EQ(result.steps.size(), 1U);
}

TEST(TestExecutorTests, CommandExceptionsBecomeErrorResults)
{
    int executions = 0;
    std::vector<CompiledStep> steps;
    steps.push_back({9, "throws", std::make_unique<TestCommand>("throws", StepResult::Pass(), executions, true)});
    RecordingEventSink sink;

    const auto result = TestExecutor{sink}.Execute(steps);

    EXPECT_EQ(result.status, RunStatus::Error);
    ASSERT_EQ(result.steps.size(), 1U);
    EXPECT_EQ(result.steps.front().result.status, StepStatus::Error);
    EXPECT_EQ(result.steps.front().result.message, "expected exception");
}

TEST(ExecutionContextTests, StoresAndEvaluatesNumericVariables)
{
    ExecutionContext context;
    context.SetVariable("counter", 4);
    context.IncrementVariable("counter");

    EXPECT_EQ(context.GetVariable("counter"), 5);
    EXPECT_TRUE(context.EvaluateCondition("counter >= 5"));
    EXPECT_FALSE(context.EvaluateCondition("counter < 0"));
}

#include "TestSupport/RecordingEventSink.h"

#include "ARTestEngine.Core/Commands/ICommand.h"
#include "ARTestEngine.Core/Execution/Cancellation.h"
#include "ARTestEngine.Core/Execution/ExecutionSession.h"
#include "ARTestEngine.Core/Execution/ExecutionStateMachine.h"
#include "ARTestEngine.Core/Execution/TestExecutor.h"
#include "ARTestEngine.Core/Instruments/InstrumentManager.h"
#include "ARTestEngine.Core/Instruments/InstrumentRegistry.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace artest;
using namespace std::chrono_literals;

namespace
{
    class ResultSequenceCommand final : public ICommand
    {
    public:
        explicit ResultSequenceCommand(std::vector<StepResult> results,
            std::shared_ptr<std::atomic_size_t> observed = {})
            : m_results(std::move(results)), m_observed(std::move(observed))
        {
        }

        std::string Name() const override { return "Test.ResultSequence"; }
        OperationResult Configure(const nlohmann::json&, std::shared_ptr<IInstrument>) override
        {
            return OperationResult::Success();
        }
        OperationResult Validate() const override { return OperationResult::Success(); }
        StepResult Execute(ExecutionContext&, const CancellationToken&) override
        {
            const auto index = m_executions.fetch_add(1);
            if (m_observed) ++*m_observed;
            return m_results[std::min(index, m_results.size() - 1)];
        }
        [[nodiscard]] std::size_t Executions() const noexcept { return m_executions.load(); }

    private:
        std::vector<StepResult> m_results;
        std::atomic_size_t m_executions{0};
        std::shared_ptr<std::atomic_size_t> m_observed;
    };

    class CooperativeDelayCommand final : public ICommand
    {
    public:
        explicit CooperativeDelayCommand(std::chrono::milliseconds duration) : m_duration(duration) {}

        std::string Name() const override { return "Test.CooperativeDelay"; }
        OperationResult Configure(const nlohmann::json&, std::shared_ptr<IInstrument>) override
        {
            return OperationResult::Success();
        }
        OperationResult Validate() const override { return OperationResult::Success(); }
        StepResult Execute(ExecutionContext&, const CancellationToken& cancellation) override
        {
            if (cancellation.WaitFor(m_duration))
            {
                return cancellation.IsTimedOut() ? StepResult::Timeout() : StepResult::Cancel();
            }
            return StepResult::Pass();
        }

    private:
        std::chrono::milliseconds m_duration;
    };

    class TrackingInstrument final : public IInstrument
    {
    public:
        static inline std::atomic_int initializeCount{0};
        static inline std::atomic_int shutdownCount{0};

        std::string GetId() const override { return m_id; }
        void SetId(std::string id) override { m_id = std::move(id); }
        OperationResult Initialize(const nlohmann::json& configuration) override
        {
            ++initializeCount;
            if (configuration.value("failInitialization", false))
            {
                return OperationResult::Failure("TEST_INITIALIZATION_FAILED", "Requested initialization failure.");
            }
            m_failShutdown = configuration.value("failShutdown", false);
            return OperationResult::Success();
        }
        OperationResult Shutdown() override
        {
            ++shutdownCount;
            return m_failShutdown
                ? OperationResult::Failure("TEST_SHUTDOWN_FAILED", "Requested cleanup failure.")
                : OperationResult::Success();
        }

    private:
        std::string m_id;
        bool m_failShutdown = false;
    };

    std::unique_ptr<InstrumentManager> MakeManager(
        InstrumentRegistry& registry,
        RecordingEventSink& sink,
        bool failShutdown = false,
        bool failInitialization = false)
    {
        EXPECT_TRUE(registry.Register(
            "Test.Tracking",
            [](IEventSink&) { return std::make_unique<TrackingInstrument>(); }).Succeeded());
        auto manager = std::make_unique<InstrumentManager>(registry, sink);
        EXPECT_TRUE(manager->LoadDefinitions({{
            "Test.Tracking",
            "tracking",
            {{"failShutdown", failShutdown}, {"failInitialization", failInitialization}}}}).Succeeded());
        return manager;
    }

    bool WaitForState(ExecutionSession& session, ExecutionState expected)
    {
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (session.State() == expected)
            {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }
}

TEST(ExecutionStateMachineTests, AcceptsOnlyDefinedTransitions)
{
    ExecutionStateMachine machine;

    EXPECT_EQ(machine.State(), ExecutionState::Idle);
    EXPECT_FALSE(machine.TryTransition(ExecutionState::Running));
    EXPECT_TRUE(machine.TryTransition(ExecutionState::Initializing));
    EXPECT_TRUE(machine.TryTransition(ExecutionState::Running));
    EXPECT_TRUE(machine.TryTransition(ExecutionState::Cancelling));
    EXPECT_TRUE(machine.TryTransition(ExecutionState::CleaningUp));
    EXPECT_TRUE(machine.TryTransition(ExecutionState::Cancelled));
    EXPECT_FALSE(machine.TryTransition(ExecutionState::Running));
}

TEST(CancellationTests, DeadlineInterruptsCooperativeWait)
{
    CancellationSource source;
    EXPECT_FALSE(source.Token().Deadline().has_value());
    const auto token = source.Token().WithTimeout(20ms);
    ASSERT_TRUE(token.Deadline().has_value());
    EXPECT_EQ(token.WithTimeout(0ms).Deadline(), token.Deadline());
    EXPECT_EQ(token.WithTimeout(1s).Deadline(), token.Deadline());
    EXPECT_LE(token.WithTimeout(1ms).Deadline(), token.Deadline());
    const auto start = std::chrono::steady_clock::now();

    EXPECT_TRUE(token.WaitFor(1s));

    EXPECT_EQ(token.Reason(), CancellationReason::TimedOut);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
}

TEST(CancellationTests, CancellationSourceWakesWaitingCommands)
{
    CancellationSource source;
    std::atomic_bool awakened{false};
    std::thread waiter([&]
    {
        awakened = source.Token().WaitFor(5s);
    });

    std::this_thread::sleep_for(10ms);
    source.Cancel();
    waiter.join();

    EXPECT_TRUE(awakened.load());
    EXPECT_EQ(source.Token().Reason(), CancellationReason::Requested);
}

TEST(ExecutionPolicyTests, RetriesUntilTheStepPasses)
{
    auto command = std::make_unique<ResultSequenceCommand>(
        std::vector<StepResult>{StepResult::Error("transient"), StepResult::Pass("recovered")});
    auto* commandView = command.get();
    RuntimeStep step{1, command->Name(), std::move(command)};
    step.policy.maxAttempts = 3;
    std::vector<RuntimeStep> steps;
    steps.push_back(std::move(step));
    RecordingEventSink sink;

    const auto run = TestExecutor{sink}.Execute(steps);

    EXPECT_TRUE(run.Succeeded());
    EXPECT_EQ(commandView->Executions(), 2U);
    ASSERT_EQ(run.steps.front().attempts.size(), 2U);
    EXPECT_EQ(run.summary.totalAttempts, 2U);
    EXPECT_EQ(sink.Count(EngineEventKind::StepRetryScheduled), 1U);
}

TEST(ExecutionPolicyTests, ContinuePolicyExecutesFollowingStepsAndPreservesFailure)
{
    auto first = std::make_unique<ResultSequenceCommand>(
        std::vector<StepResult>{StepResult::Fail("expected")});
    auto second = std::make_unique<ResultSequenceCommand>(
        std::vector<StepResult>{StepResult::Pass()});
    RuntimeStep firstStep{1, first->Name(), std::move(first)};
    firstStep.policy.onFailure = FailureAction::Continue;
    std::vector<RuntimeStep> steps;
    steps.push_back(std::move(firstStep));
    steps.push_back({2, second->Name(), std::move(second)});
    RecordingEventSink sink;

    const auto run = TestExecutor{sink}.Execute(steps);

    EXPECT_EQ(run.status, RunStatus::Failed);
    EXPECT_EQ(run.steps.size(), 2U);
    EXPECT_EQ(run.summary.failedSteps, 1U);
    EXPECT_EQ(run.summary.passedSteps, 1U);
    EXPECT_EQ(run.summary.skippedSteps, 0U);
}

TEST(ExecutionPolicyTests, StopPolicyCountsRemainingStepsAsSkipped)
{
    auto first = std::make_unique<ResultSequenceCommand>(
        std::vector<StepResult>{StepResult::Error("fatal")});
    auto second = std::make_unique<ResultSequenceCommand>(
        std::vector<StepResult>{StepResult::Pass()});
    std::vector<RuntimeStep> steps;
    steps.push_back({1, first->Name(), std::move(first)});
    steps.push_back({2, second->Name(), std::move(second)});
    RecordingEventSink sink;

    const auto run = TestExecutor{sink}.Execute(steps);

    EXPECT_EQ(run.status, RunStatus::Error);
    EXPECT_EQ(run.steps.size(), 1U);
    EXPECT_EQ(run.summary.executedSteps, 1U);
    EXPECT_EQ(run.summary.skippedSteps, 1U);
}

TEST(ExecutionPolicyTests, CooperativeTimeoutProducesTimedOutVerdict)
{
    auto command = std::make_unique<CooperativeDelayCommand>(1s);
    RuntimeStep step{1, command->Name(), std::move(command)};
    step.policy.timeout = 20ms;
    std::vector<RuntimeStep> steps;
    steps.push_back(std::move(step));
    RecordingEventSink sink;

    const auto run = TestExecutor{sink}.Execute(steps);

    EXPECT_EQ(run.status, RunStatus::TimedOut);
    ASSERT_EQ(run.steps.size(), 1U);
    EXPECT_EQ(run.steps.front().result.status, StepStatus::TimedOut);
    EXPECT_EQ(run.summary.timedOutSteps, 1U);
}

TEST(ExecutionSessionTests, RunsAsynchronouslyAndGuaranteesCleanup)
{
    TrackingInstrument::initializeCount = 0;
    TrackingInstrument::shutdownCount = 0;
    RecordingEventSink sink;
    InstrumentRegistry registry;
    auto manager = MakeManager(registry, sink);
    RunToCompletionControl control;
    auto command = std::make_unique<CooperativeDelayCommand>(20ms);
    std::vector<RuntimeStep> steps;
    steps.push_back({1, command->Name(), std::move(command)});
    ExecutionSession session(std::move(steps), *manager, sink, control);

    ASSERT_TRUE(session.Start().Succeeded());
    const auto run = session.Wait();

    EXPECT_TRUE(run.Succeeded());
    EXPECT_EQ(session.State(), ExecutionState::Completed);
    EXPECT_EQ(TrackingInstrument::initializeCount.load(), 1);
    EXPECT_EQ(TrackingInstrument::shutdownCount.load(), 1);
    EXPECT_GE(sink.Count(EngineEventKind::RunStateChanged), 4U);
}

TEST(ExecutionSessionTests, CancellationStopsTheWorkerAndStillCleansUp)
{
    TrackingInstrument::initializeCount = 0;
    TrackingInstrument::shutdownCount = 0;
    RecordingEventSink sink;
    InstrumentRegistry registry;
    auto manager = MakeManager(registry, sink);
    RunToCompletionControl control;
    auto command = std::make_unique<CooperativeDelayCommand>(5s);
    std::vector<RuntimeStep> steps;
    steps.push_back({1, command->Name(), std::move(command)});
    ExecutionSession session(std::move(steps), *manager, sink, control);
    ASSERT_TRUE(session.Start().Succeeded());
    ASSERT_TRUE(WaitForState(session, ExecutionState::Running));

    session.Cancel();
    const auto run = session.Wait();

    EXPECT_EQ(run.status, RunStatus::Cancelled);
    EXPECT_EQ(session.State(), ExecutionState::Cancelled);
    EXPECT_EQ(TrackingInstrument::shutdownCount.load(), 1);
}

TEST(ExecutionSessionTests, CleanupFailureChangesTheOverallVerdict)
{
    TrackingInstrument::initializeCount = 0;
    TrackingInstrument::shutdownCount = 0;
    RecordingEventSink sink;
    InstrumentRegistry registry;
    auto manager = MakeManager(registry, sink, true);
    RunToCompletionControl control;
    auto command = std::make_unique<ResultSequenceCommand>(
        std::vector<StepResult>{StepResult::Pass()});
    std::vector<RuntimeStep> steps;
    steps.push_back({1, command->Name(), std::move(command)});
    ExecutionSession session(std::move(steps), *manager, sink, control);

    ASSERT_TRUE(session.Start().Succeeded());
    const auto run = session.Wait();

    EXPECT_EQ(run.status, RunStatus::Error);
    EXPECT_EQ(run.failureKind, RunFailureKind::Cleanup);
    EXPECT_EQ(session.State(), ExecutionState::Failed);
    ASSERT_FALSE(run.diagnostics.empty());
    EXPECT_EQ(run.diagnostics.front().code, "TEST_SHUTDOWN_FAILED");
}

TEST(ExecutionSessionTests, InitializationFailureHasItsOwnFailureKindAndTerminalState)
{
    TrackingInstrument::initializeCount = 0;
    TrackingInstrument::shutdownCount = 0;
    RecordingEventSink sink;
    InstrumentRegistry registry;
    auto manager = MakeManager(registry, sink, false, true);
    RunToCompletionControl control;
    auto executions = std::make_shared<std::atomic_size_t>(0);
    auto command = std::make_unique<ResultSequenceCommand>(
        std::vector<StepResult>{StepResult::Pass()}, executions);
    std::vector<RuntimeStep> steps;
    steps.push_back({1, command->Name(), std::move(command)});
    ExecutionSession session(std::move(steps), *manager, sink, control);

    ASSERT_TRUE(session.Start().Succeeded());
    const auto run = session.Wait();

    EXPECT_EQ(run.status, RunStatus::Error);
    EXPECT_EQ(run.failureKind, RunFailureKind::Initialization);
    EXPECT_EQ(session.State(), ExecutionState::Failed);
    EXPECT_EQ(executions->load(), 0U);
    EXPECT_EQ(run.summary.skippedSteps, 1U);
}

TEST(ExecutionSessionTests, SessionCanOnlyBeStartedOnce)
{
    RecordingEventSink sink;
    InstrumentRegistry registry;
    InstrumentManager manager(registry, sink);
    ASSERT_TRUE(manager.LoadDefinitions({}).Succeeded());
    RunToCompletionControl control;
    auto command = std::make_unique<ResultSequenceCommand>(
        std::vector<StepResult>{StepResult::Pass()});
    std::vector<RuntimeStep> steps;
    steps.push_back({1, command->Name(), std::move(command)});
    ExecutionSession session(std::move(steps), manager, sink, control);

    ASSERT_TRUE(session.Start().Succeeded());
    const auto secondStart = session.Start();
    const auto run = session.Wait();

    EXPECT_FALSE(secondStart.Succeeded());
    EXPECT_EQ(secondStart.diagnostics.front().code, "EXECUTION_SESSION_ALREADY_STARTED");
    EXPECT_TRUE(run.Succeeded());
}

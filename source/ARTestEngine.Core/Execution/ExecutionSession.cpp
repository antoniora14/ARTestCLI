#include "ExecutionSession.h"

#include "ExecutionContext.h"
#include "TestExecutor.h"

#include <chrono>
#include <exception>
#include <iterator>
#include <sstream>
#include <utility>

namespace
{
    class SessionEventSink final : public artest::IEventSink
    {
    public:
        explicit SessionEventSink(artest::IEventSink& target) noexcept : m_target(target) {}

        void Publish(const artest::EngineEvent& event) noexcept override
        {
            if (event.kind != artest::EngineEventKind::RunCompleted)
            {
                m_target.Publish(event);
            }
        }

    private:
        artest::IEventSink& m_target;
    };

    [[nodiscard]] std::string FinalSummary(const artest::RunResult& run)
    {
        const auto status = [&run]
        {
            switch (run.status)
            {
            case artest::RunStatus::Passed: return "PASSED";
            case artest::RunStatus::Failed: return "FAILED";
            case artest::RunStatus::Error: return "ERROR";
            case artest::RunStatus::Cancelled: return "CANCELLED";
            case artest::RunStatus::TimedOut: return "TIMED_OUT";
            }
            return "UNKNOWN";
        }();

        std::ostringstream text;
        text << status
             << " | planned=" << run.summary.plannedSteps
             << " executed=" << run.summary.executedSteps
             << " passed=" << run.summary.passedSteps
             << " failed=" << run.summary.failedSteps
             << " errors=" << run.summary.errorSteps
             << " timedOut=" << run.summary.timedOutSteps
             << " cancelled=" << run.summary.cancelledSteps
             << " skipped=" << run.summary.skippedSteps
             << " attempts=" << run.summary.totalAttempts
             << " durationMs=" << run.summary.duration.count();
        return text.str();
    }
}

namespace artest
{
    ExecutionSession::ExecutionSession(
        std::vector<CompiledStep> steps,
        InstrumentManager& instruments,
        IEventSink& eventSink,
        IExecutionControl& executionControl)
        : m_steps(std::move(steps)),
          m_instruments(instruments),
          m_eventSink(eventSink),
          m_executionControl(executionControl)
    {
    }

    ExecutionSession::~ExecutionSession()
    {
        Cancel();
        try
        {
            if (m_future.valid())
            {
                static_cast<void>(Wait());
            }
        }
        catch (...)
        {
        }
    }

    OperationResult ExecutionSession::Start()
    {
        std::scoped_lock lock{m_waitMutex};
        if (!TransitionTo(ExecutionState::Initializing))
        {
            return OperationResult::Failure(
                "EXECUTION_SESSION_ALREADY_STARTED",
                "The execution session can only be started once.");
        }

        try
        {
            m_future = std::async(std::launch::async, [this]
            {
                return RunWorker();
            });
            return OperationResult::Success();
        }
        catch (const std::exception& exception)
        {
            static_cast<void>(TransitionTo(ExecutionState::CleaningUp));
            static_cast<void>(TransitionTo(ExecutionState::Failed));
            return OperationResult::Failure("EXECUTION_ASYNC_START_FAILED", exception.what());
        }
        catch (...)
        {
            static_cast<void>(TransitionTo(ExecutionState::CleaningUp));
            static_cast<void>(TransitionTo(ExecutionState::Failed));
            return OperationResult::Failure(
                "EXECUTION_ASYNC_START_FAILED",
                "Unknown failure while starting asynchronous execution.");
        }
    }

    void ExecutionSession::Cancel() noexcept
    {
        // Cancellation may originate on a Windows console-control thread.
        // Only signal the cooperative token here; the worker owns all state
        // transitions and event publication so sinks are never invoked from
        // two session threads concurrently.
        m_cancellation.Cancel();
    }

    bool ExecutionSession::WaitFor(std::chrono::milliseconds timeout)
    {
        std::scoped_lock lock{m_waitMutex};
        if (m_result.has_value())
        {
            return true;
        }
        if (!m_future.valid())
        {
            return false;
        }
        return m_future.wait_for(timeout) == std::future_status::ready;
    }

    RunResult ExecutionSession::Wait()
    {
        std::scoped_lock lock{m_waitMutex};
        if (m_result.has_value())
        {
            return *m_result;
        }
        if (!m_future.valid())
        {
            RunResult result;
            result.status = RunStatus::Error;
            result.failureKind = RunFailureKind::Internal;
            result.summary.plannedSteps = m_steps.size();
            result.summary.skippedSteps = m_steps.size();
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "EXECUTION_SESSION_NOT_STARTED",
                "The execution session was not started.",
                {}});
            return result;
        }

        m_result = m_future.get();
        return *m_result;
    }

    ExecutionState ExecutionSession::State() const noexcept
    {
        return m_stateMachine.State();
    }

    RunResult ExecutionSession::RunWorker() noexcept
    {
        const auto sessionStart = std::chrono::steady_clock::now();
        RunResult run;
        run.summary.plannedSteps = m_steps.size();
        run.summary.skippedSteps = m_steps.size();

        try
        {
            auto initialization = m_instruments.InitializeAll();
            if (!initialization.Succeeded())
            {
                run.status = RunStatus::Error;
                run.failureKind = RunFailureKind::Initialization;
                run.diagnostics = std::move(initialization.diagnostics);
                PublishDiagnostics(run.diagnostics);
            }
            else if (m_cancellation.IsCancellationRequested())
            {
                run.status = RunStatus::Cancelled;
                run.failureKind = RunFailureKind::Execution;
            }
            else if (!TransitionTo(ExecutionState::Running))
            {
                if (m_cancellation.IsCancellationRequested())
                {
                    run.status = RunStatus::Cancelled;
                    run.failureKind = RunFailureKind::Execution;
                }
                else
                {
                    run.status = RunStatus::Error;
                    run.failureKind = RunFailureKind::Internal;
                    run.diagnostics.push_back({
                        DiagnosticSeverity::Error,
                        "EXECUTION_STATE_TRANSITION_INVALID",
                        "The session could not transition to RUNNING.",
                        {}});
                }
            }
            else
            {
                SessionEventSink sessionSink{m_eventSink};
                TestExecutor executor{sessionSink};
                ExecutionContext context;
                run = executor.Execute(
                    m_steps,
                    context,
                    m_executionControl,
                    m_cancellation.Token());
            }
        }
        catch (const std::exception& exception)
        {
            run.status = RunStatus::Error;
            run.failureKind = RunFailureKind::Internal;
            run.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "EXECUTION_SESSION_EXCEPTION",
                exception.what(),
                {}});
            PublishDiagnostics(run.diagnostics);
        }
        catch (...)
        {
            run.status = RunStatus::Error;
            run.failureKind = RunFailureKind::Internal;
            run.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "EXECUTION_SESSION_EXCEPTION",
                "Unknown exception escaped the execution session.",
                {}});
            PublishDiagnostics(run.diagnostics);
        }

        auto currentState = State();
        if (run.status == RunStatus::Cancelled
            && (currentState == ExecutionState::Initializing
                || currentState == ExecutionState::Running))
        {
            static_cast<void>(TransitionTo(ExecutionState::Cancelling));
            currentState = State();
        }
        if (currentState == ExecutionState::Initializing
            || currentState == ExecutionState::Running
            || currentState == ExecutionState::Cancelling)
        {
            static_cast<void>(TransitionTo(ExecutionState::CleaningUp));
        }

        OperationResult cleanup;
        try
        {
            cleanup = m_instruments.ShutdownAll();
        }
        catch (const std::exception& exception)
        {
            cleanup = OperationResult::Failure("INSTRUMENT_CLEANUP_EXCEPTION", exception.what());
        }
        catch (...)
        {
            cleanup = OperationResult::Failure(
                "INSTRUMENT_CLEANUP_EXCEPTION",
                "Unknown exception while cleaning up instruments.");
        }
        if (!cleanup.Succeeded())
        {
            PublishDiagnostics(cleanup.diagnostics);
            run.diagnostics.insert(
                run.diagnostics.end(),
                std::make_move_iterator(cleanup.diagnostics.begin()),
                std::make_move_iterator(cleanup.diagnostics.end()));
            run.status = RunStatus::Error;
            run.failureKind = RunFailureKind::Cleanup;
        }

        run.summary.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - sessionStart);
        const auto terminalState = [&run]
        {
            switch (run.status)
            {
            case RunStatus::Passed: return ExecutionState::Completed;
            case RunStatus::Cancelled: return ExecutionState::Cancelled;
            case RunStatus::TimedOut: return ExecutionState::TimedOut;
            case RunStatus::Failed:
            case RunStatus::Error:
                return ExecutionState::Failed;
            }
            return ExecutionState::Failed;
        }();
        static_cast<void>(TransitionTo(terminalState));

        m_eventSink.Publish({
            EngineEventKind::RunCompleted,
            run.Succeeded() ? EngineEventSeverity::Information : EngineEventSeverity::Error,
            "execution-session",
            FinalSummary(run),
            std::nullopt,
            std::nullopt,
            run.status,
            std::nullopt,
            run.summary.duration});
        return run;
    }

    bool ExecutionSession::TransitionTo(ExecutionState state) noexcept
    {
        if (!m_stateMachine.TryTransition(state))
        {
            return false;
        }
        m_eventSink.Publish({
            EngineEventKind::RunStateChanged,
            EngineEventSeverity::Information,
            "execution-session",
            ToString(state)});
        return true;
    }

    void ExecutionSession::PublishDiagnostics(const std::vector<Diagnostic>& diagnostics) noexcept
    {
        for (const auto& diagnostic : diagnostics)
        {
            m_eventSink.Publish({
                EngineEventKind::Diagnostic,
                diagnostic.severity == DiagnosticSeverity::Error
                    ? EngineEventSeverity::Error
                    : EngineEventSeverity::Warning,
                diagnostic.code,
                diagnostic.location.empty()
                    ? diagnostic.message
                    : diagnostic.location + ": " + diagnostic.message});
        }
    }
}

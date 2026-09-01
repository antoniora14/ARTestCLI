#include "TestExecutor.h"

#include <chrono>
#include <exception>
#include <sstream>
#include <string>

namespace
{
    [[nodiscard]] std::string RunStatusText(artest::RunStatus status)
    {
        switch (status)
        {
        case artest::RunStatus::Passed: return "PASSED";
        case artest::RunStatus::Failed: return "FAILED";
        case artest::RunStatus::Error: return "ERROR";
        case artest::RunStatus::Cancelled: return "CANCELLED";
        case artest::RunStatus::TimedOut: return "TIMED_OUT";
        }
        return "UNKNOWN";
    }

    [[nodiscard]] artest::RunStatus RunStatusFor(artest::StepStatus status)
    {
        switch (status)
        {
        case artest::StepStatus::Passed: return artest::RunStatus::Passed;
        case artest::StepStatus::Failed: return artest::RunStatus::Failed;
        case artest::StepStatus::Cancelled: return artest::RunStatus::Cancelled;
        case artest::StepStatus::TimedOut: return artest::RunStatus::TimedOut;
        case artest::StepStatus::Error:
        case artest::StepStatus::Skipped:
            return artest::RunStatus::Error;
        }
        return artest::RunStatus::Error;
    }

    [[nodiscard]] int Severity(artest::RunStatus status)
    {
        switch (status)
        {
        case artest::RunStatus::Passed: return 0;
        case artest::RunStatus::Failed: return 1;
        case artest::RunStatus::Error: return 2;
        case artest::RunStatus::TimedOut: return 3;
        case artest::RunStatus::Cancelled: return 4;
        }
        return 4;
    }

    void AccumulateStatus(artest::RunResult& run, artest::StepStatus status)
    {
        const auto candidate = RunStatusFor(status);
        if (Severity(candidate) > Severity(run.status))
        {
            run.status = candidate;
        }
    }

    void BuildSummary(artest::RunResult& run, std::size_t plannedSteps)
    {
        run.summary.plannedSteps = plannedSteps;
        run.summary.executedSteps = run.steps.size();
        run.summary.skippedSteps = plannedSteps - run.steps.size();

        for (const auto& step : run.steps)
        {
            run.summary.totalAttempts += step.attempts.size();
            switch (step.result.status)
            {
            case artest::StepStatus::Passed: ++run.summary.passedSteps; break;
            case artest::StepStatus::Failed: ++run.summary.failedSteps; break;
            case artest::StepStatus::Error: ++run.summary.errorSteps; break;
            case artest::StepStatus::Cancelled: ++run.summary.cancelledSteps; break;
            case artest::StepStatus::TimedOut: ++run.summary.timedOutSteps; break;
            case artest::StepStatus::Skipped: ++run.summary.skippedSteps; break;
            }
        }
    }

    [[nodiscard]] std::string SummaryText(const artest::RunResult& run)
    {
        std::ostringstream text;
        text << RunStatusText(run.status)
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
    TestExecutor::TestExecutor(IEventSink& eventSink)
        : m_eventSink(eventSink)
    {
    }

    RunResult TestExecutor::Execute(std::vector<CompiledStep>& steps)
    {
        ExecutionContext context;
        RunToCompletionControl control;
        CancellationSource cancellation;
        return Execute(steps, context, control, cancellation.Token());
    }

    RunResult TestExecutor::Execute(
        std::vector<CompiledStep>& steps,
        ExecutionContext& context,
        IExecutionControl& control)
    {
        CancellationSource cancellation;
        return Execute(steps, context, control, cancellation.Token());
    }

    RunResult TestExecutor::Execute(
        std::vector<CompiledStep>& steps,
        ExecutionContext& context,
        IExecutionControl& control,
        const CancellationToken& cancellation)
    {
        RunResult run;
        const auto runStart = std::chrono::steady_clock::now();

        for (std::size_t index = 0; index < steps.size(); ++index)
        {
            auto& step = steps[index];
            if (cancellation.IsCancellationRequested())
            {
                run.status = RunStatus::Cancelled;
                run.failureKind = RunFailureKind::Execution;
                break;
            }

            try
            {
                const StepExecutionInfo information{index, step.stepId, step.commandName};
                if (control.BeforeStep(information) == ExecutionDecision::Cancel)
                {
                    run.status = RunStatus::Cancelled;
                    run.failureKind = RunFailureKind::Execution;
                    break;
                }
            }
            catch (const std::exception& exception)
            {
                run.status = RunStatus::Error;
                run.failureKind = RunFailureKind::Internal;
                run.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "EXECUTION_CONTROL_EXCEPTION",
                    exception.what(),
                    "stepId=" + std::to_string(step.stepId)});
                m_eventSink.Publish({
                    EngineEventKind::Diagnostic,
                    EngineEventSeverity::Error,
                    "EXECUTION_CONTROL_EXCEPTION",
                    exception.what(),
                    step.stepId});
                break;
            }
            catch (...)
            {
                run.status = RunStatus::Error;
                run.failureKind = RunFailureKind::Internal;
                run.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "EXECUTION_CONTROL_EXCEPTION",
                    "Unknown exception from the execution control.",
                    "stepId=" + std::to_string(step.stepId)});
                break;
            }

            m_eventSink.Publish({
                EngineEventKind::StepStarted,
                EngineEventSeverity::Information,
                step.commandName,
                "Executing step.",
                step.stepId});

            const auto stepStart = std::chrono::steady_clock::now();
            StepExecutionRecord record;
            record.stepId = step.stepId;
            record.commandName = step.commandName;

            // CompiledStep is a public model. The compiler rejects invalid
            // policies, but the executor still protects direct API callers.
            const int maximumAttempts = step.policy.maxAttempts < 1
                ? 1
                : step.policy.maxAttempts;
            for (int attempt = 1; attempt <= maximumAttempts; ++attempt)
            {
                const auto attemptNumber = static_cast<std::size_t>(attempt);
                const auto attemptToken = cancellation.WithTimeout(step.policy.timeout);
                m_eventSink.Publish({
                    EngineEventKind::StepAttemptStarted,
                    EngineEventSeverity::Information,
                    step.commandName,
                    "Starting step attempt.",
                    step.stepId,
                    std::nullopt,
                    std::nullopt,
                    attemptNumber});

                const auto attemptStart = std::chrono::steady_clock::now();
                StepResult attemptResult;
                try
                {
                    attemptResult = step.command->Execute(context, attemptToken);
                }
                catch (const std::exception& exception)
                {
                    attemptResult = StepResult::Error(exception.what());
                }
                catch (...)
                {
                    attemptResult = StepResult::Error("Unknown exception while executing the step.");
                }

                if (cancellation.IsCancellationRequested())
                {
                    attemptResult = StepResult::Cancel();
                }
                else if (attemptToken.IsTimedOut())
                {
                    attemptResult = StepResult::Timeout();
                }

                const auto attemptDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - attemptStart);
                record.attempts.push_back({attemptNumber, attemptResult, attemptDuration});
                record.result = attemptResult;

                if (attemptResult.Succeeded()
                    || attemptResult.status == StepStatus::Cancelled
                    || attempt >= maximumAttempts)
                {
                    break;
                }

                m_eventSink.Publish({
                    EngineEventKind::StepRetryScheduled,
                    EngineEventSeverity::Warning,
                    step.commandName,
                    "Retrying the step after a failed attempt.",
                    step.stepId,
                    attemptResult.status,
                    std::nullopt,
                    attemptNumber,
                    step.policy.retryDelay});

                if (cancellation.WaitFor(step.policy.retryDelay))
                {
                    record.result = StepResult::Cancel();
                    break;
                }
            }

            record.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stepStart);
            const auto finalStatus = record.result.status;
            const auto finalMessage = record.result.message;
            const auto attempts = record.attempts.size();
            run.steps.push_back(std::move(record));

            m_eventSink.Publish({
                EngineEventKind::StepCompleted,
                finalStatus == StepStatus::Passed
                    ? EngineEventSeverity::Information
                    : EngineEventSeverity::Error,
                step.commandName,
                finalMessage,
                step.stepId,
                finalStatus,
                std::nullopt,
                attempts,
                run.steps.back().duration});

            AccumulateStatus(run, finalStatus);
            if (finalStatus != StepStatus::Passed)
            {
                run.failureKind = RunFailureKind::Execution;
                if (finalStatus == StepStatus::Cancelled
                    || step.policy.onFailure == FailureAction::Stop)
                {
                    break;
                }
            }
        }

        run.summary.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - runStart);
        BuildSummary(run, steps.size());
        m_eventSink.Publish({
            EngineEventKind::RunCompleted,
            run.Succeeded() ? EngineEventSeverity::Information : EngineEventSeverity::Error,
            "executor",
            SummaryText(run),
            std::nullopt,
            std::nullopt,
            run.status,
            std::nullopt,
            run.summary.duration});
        return run;
    }
}

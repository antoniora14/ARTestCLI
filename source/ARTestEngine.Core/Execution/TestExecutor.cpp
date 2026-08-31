#include "TestExecutor.h"

#include <chrono>
#include <exception>
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
        }
        return "UNKNOWN";
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
        return Execute(steps, context, control);
    }

    RunResult TestExecutor::Execute(
        std::vector<CompiledStep>& steps,
        ExecutionContext& context,
        IExecutionControl& control)
    {
        RunResult run;

        for (std::size_t index = 0; index < steps.size(); ++index)
        {
            auto& step = steps[index];
            try
            {
                const StepExecutionInfo information{
                    index,
                    step.stepId,
                    step.commandName};
                if (control.BeforeStep(information) == ExecutionDecision::Cancel)
                {
                    run.status = RunStatus::Cancelled;
                    break;
                }
            }
            catch (const std::exception& exception)
            {
                run.status = RunStatus::Error;
                m_eventSink.Publish({
                    EngineEventKind::Diagnostic,
                    EngineEventSeverity::Error,
                    "execution-control",
                    exception.what()});
                break;
            }
            catch (...)
            {
                run.status = RunStatus::Error;
                m_eventSink.Publish({
                    EngineEventKind::Diagnostic,
                    EngineEventSeverity::Error,
                    "execution-control",
                    "Unknown exception from the execution control."});
                break;
            }

            m_eventSink.Publish({
                EngineEventKind::StepStarted,
                EngineEventSeverity::Information,
                step.commandName,
                "Executing step.",
                step.stepId});

            const auto start = std::chrono::steady_clock::now();
            StepResult stepResult;
            try
            {
                stepResult = step.command->Execute(context);
            }
            catch (const std::exception& exception)
            {
                stepResult = StepResult::Error(exception.what());
            }
            catch (...)
            {
                stepResult = StepResult::Error(
                    "Unknown exception while executing the step.");
            }

            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            run.steps.push_back({step.stepId, step.commandName, stepResult, duration});

            m_eventSink.Publish({
                EngineEventKind::StepCompleted,
                stepResult.Succeeded()
                    ? EngineEventSeverity::Information
                    : EngineEventSeverity::Error,
                step.commandName,
                stepResult.message,
                step.stepId,
                stepResult.status});

            if (!stepResult.Succeeded())
            {
                run.status = stepResult.status == StepStatus::Failed
                    ? RunStatus::Failed
                    : RunStatus::Error;
                break;
            }
        }

        m_eventSink.Publish({
            EngineEventKind::RunCompleted,
            run.Succeeded()
                ? EngineEventSeverity::Information
                : EngineEventSeverity::Error,
            "executor",
            RunStatusText(run.status),
            std::nullopt,
            std::nullopt,
            run.status});
        return run;
    }
}

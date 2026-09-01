#pragma once

#include "../Diagnostics.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace artest
{
    enum class StepStatus
    {
        Passed,
        Failed,
        Error,
        Skipped,
        Cancelled,
        TimedOut
    };

    struct StepResult
    {
        StepStatus status = StepStatus::Passed;
        std::string message;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == StepStatus::Passed;
        }

        [[nodiscard]] static StepResult Pass(std::string message = {})
        {
            return {StepStatus::Passed, std::move(message)};
        }

        [[nodiscard]] static StepResult Fail(std::string message)
        {
            return {StepStatus::Failed, std::move(message)};
        }

        [[nodiscard]] static StepResult Error(std::string message)
        {
            return {StepStatus::Error, std::move(message)};
        }

        [[nodiscard]] static StepResult Cancel(std::string message = "Execution was cancelled.")
        {
            return {StepStatus::Cancelled, std::move(message)};
        }

        [[nodiscard]] static StepResult Timeout(std::string message = "The step timed out.")
        {
            return {StepStatus::TimedOut, std::move(message)};
        }
    };

    struct StepAttemptRecord
    {
        std::size_t attempt = 0;
        StepResult result;
        std::chrono::milliseconds duration{0};
    };

    struct StepExecutionRecord
    {
        std::uint64_t stepId = 0;
        std::string commandName;
        StepResult result;
        std::chrono::milliseconds duration{0};
        std::vector<StepAttemptRecord> attempts;
    };

    enum class RunStatus
    {
        Passed,
        Failed,
        Error,
        Cancelled,
        TimedOut
    };

    enum class RunFailureKind
    {
        None,
        Initialization,
        Execution,
        Cleanup,
        Internal
    };

    struct RunSummary
    {
        std::size_t plannedSteps = 0;
        std::size_t executedSteps = 0;
        std::size_t passedSteps = 0;
        std::size_t failedSteps = 0;
        std::size_t errorSteps = 0;
        std::size_t cancelledSteps = 0;
        std::size_t timedOutSteps = 0;
        std::size_t skippedSteps = 0;
        std::size_t totalAttempts = 0;
        std::chrono::milliseconds duration{0};
    };

    struct RunResult
    {
        RunStatus status = RunStatus::Passed;
        RunFailureKind failureKind = RunFailureKind::None;
        std::vector<StepExecutionRecord> steps;
        std::vector<Diagnostic> diagnostics;
        RunSummary summary;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == RunStatus::Passed;
        }
    };
}

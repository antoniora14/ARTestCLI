#pragma once

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
    };

    struct StepExecutionRecord
    {
        std::uint64_t stepId = 0;
        std::string commandName;
        StepResult result;
        std::chrono::milliseconds duration{0};
    };

    enum class RunStatus
    {
        Passed,
        Failed,
        Error,
        Cancelled
    };

    struct RunResult
    {
        RunStatus status = RunStatus::Passed;
        std::vector<StepExecutionRecord> steps;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == RunStatus::Passed;
        }
    };
}

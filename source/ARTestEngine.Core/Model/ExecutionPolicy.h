#pragma once

#include <chrono>

namespace artest
{
    enum class FailureAction
    {
        Stop,
        Continue
    };

    struct StepExecutionPolicy
    {
        int maxAttempts = 1;
        std::chrono::milliseconds retryDelay{0};
        std::chrono::milliseconds timeout{0};
        FailureAction onFailure = FailureAction::Stop;
    };
}

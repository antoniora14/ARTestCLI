#pragma once

#include <mutex>
#include <string>

namespace artest
{
    enum class ExecutionState
    {
        Idle,
        Initializing,
        Running,
        Cancelling,
        CleaningUp,
        Completed,
        Failed,
        Cancelled,
        TimedOut
    };

    [[nodiscard]] std::string ToString(ExecutionState state);

    class ExecutionStateMachine final
    {
    public:
        [[nodiscard]] ExecutionState State() const noexcept;
        [[nodiscard]] bool TryTransition(ExecutionState next) noexcept;

    private:
        [[nodiscard]] static bool IsAllowed(ExecutionState current, ExecutionState next) noexcept;

        mutable std::mutex m_mutex;
        ExecutionState m_state = ExecutionState::Idle;
    };
}

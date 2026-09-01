#include "ExecutionStateMachine.h"

namespace artest
{
    std::string ToString(ExecutionState state)
    {
        switch (state)
        {
        case ExecutionState::Idle: return "IDLE";
        case ExecutionState::Initializing: return "INITIALIZING";
        case ExecutionState::Running: return "RUNNING";
        case ExecutionState::Cancelling: return "CANCELLING";
        case ExecutionState::CleaningUp: return "CLEANING_UP";
        case ExecutionState::Completed: return "COMPLETED";
        case ExecutionState::Failed: return "FAILED";
        case ExecutionState::Cancelled: return "CANCELLED";
        case ExecutionState::TimedOut: return "TIMED_OUT";
        }
        return "UNKNOWN";
    }

    ExecutionState ExecutionStateMachine::State() const noexcept
    {
        std::scoped_lock lock{m_mutex};
        return m_state;
    }

    bool ExecutionStateMachine::TryTransition(ExecutionState next) noexcept
    {
        std::scoped_lock lock{m_mutex};
        if (!IsAllowed(m_state, next))
        {
            return false;
        }
        m_state = next;
        return true;
    }

    bool ExecutionStateMachine::IsAllowed(ExecutionState current, ExecutionState next) noexcept
    {
        switch (current)
        {
        case ExecutionState::Idle:
            return next == ExecutionState::Initializing;
        case ExecutionState::Initializing:
            return next == ExecutionState::Running
                || next == ExecutionState::Cancelling
                || next == ExecutionState::CleaningUp;
        case ExecutionState::Running:
            return next == ExecutionState::Cancelling
                || next == ExecutionState::CleaningUp;
        case ExecutionState::Cancelling:
            return next == ExecutionState::CleaningUp;
        case ExecutionState::CleaningUp:
            return next == ExecutionState::Completed
                || next == ExecutionState::Failed
                || next == ExecutionState::Cancelled
                || next == ExecutionState::TimedOut;
        case ExecutionState::Completed:
        case ExecutionState::Failed:
        case ExecutionState::Cancelled:
        case ExecutionState::TimedOut:
            return false;
        }
        return false;
    }
}

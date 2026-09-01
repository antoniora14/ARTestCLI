#pragma once

#include "Cancellation.h"
#include "ExecutionResult.h"
#include "ExecutionStateMachine.h"
#include "IEventSink.h"
#include "IExecutionControl.h"
#include "../Diagnostics.h"
#include "../Instruments/InstrumentManager.h"
#include "../Model/CompiledStep.h"

#include <future>
#include <mutex>
#include <optional>
#include <vector>

namespace artest
{
    class ExecutionSession final
    {
    public:
        ExecutionSession(
            std::vector<CompiledStep> steps,
            InstrumentManager& instruments,
            IEventSink& eventSink,
            IExecutionControl& executionControl);
        ~ExecutionSession();

        ExecutionSession(const ExecutionSession&) = delete;
        ExecutionSession& operator=(const ExecutionSession&) = delete;

        [[nodiscard]] OperationResult Start();
        void Cancel() noexcept;
        [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout);
        [[nodiscard]] RunResult Wait();
        [[nodiscard]] ExecutionState State() const noexcept;

    private:
        [[nodiscard]] RunResult RunWorker() noexcept;
        bool TransitionTo(ExecutionState state) noexcept;
        void PublishDiagnostics(const std::vector<Diagnostic>& diagnostics) noexcept;

        std::vector<CompiledStep> m_steps;
        InstrumentManager& m_instruments;
        IEventSink& m_eventSink;
        IExecutionControl& m_executionControl;
        CancellationSource m_cancellation;
        ExecutionStateMachine m_stateMachine;
        std::future<RunResult> m_future;
        std::optional<RunResult> m_result;
        mutable std::mutex m_waitMutex;
    };
}

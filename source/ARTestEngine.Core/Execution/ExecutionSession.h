#pragma once

#include "Cancellation.h"
#include "ExecutionResult.h"
#include "ExecutionStateMachine.h"
#include "IEventSink.h"
#include "IExecutionControl.h"
#include "../Diagnostics.h"
#include "../Instruments/InstrumentManager.h"
#include "../Model/CompiledStep.h"
#include "RuntimeStep.h"
#include "../Commands/CommandRegistry.h"

#include <future>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace artest
{
    class ExecutionSession final
    {
    public:
        ExecutionSession(
            std::vector<RuntimeStep> steps,
            InstrumentManager& instruments,
            IEventSink& eventSink,
            IExecutionControl& executionControl);
        ExecutionSession(
            std::vector<CompiledStep> steps,
            CommandRegistry& commands,
            InstrumentManager& instruments,
            IEventSink& eventSink,
            IExecutionControl& executionControl,
            std::function<OperationResult()> prepareRuntime);
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

        std::vector<RuntimeStep> m_steps;
        std::vector<CompiledStep> m_compiledSteps;
        CommandRegistry* m_commands = nullptr;
        std::size_t m_plannedSteps = 0;
        std::function<OperationResult()> m_prepareRuntime;
        [[nodiscard]] OperationResult BindCommands();
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

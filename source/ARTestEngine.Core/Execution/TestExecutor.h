#pragma once

#include "ExecutionContext.h"
#include "ExecutionResult.h"
#include "Cancellation.h"
#include "IEventSink.h"
#include "IExecutionControl.h"
#include "../Model/CompiledStep.h"

#include <vector>

namespace artest
{
    class TestExecutor final
    {
    public:
        explicit TestExecutor(IEventSink& eventSink);

        [[nodiscard]] RunResult Execute(std::vector<CompiledStep>& steps);
        [[nodiscard]] RunResult Execute(
            std::vector<CompiledStep>& steps,
            ExecutionContext& context,
            IExecutionControl& control);
        [[nodiscard]] RunResult Execute(
            std::vector<CompiledStep>& steps,
            ExecutionContext& context,
            IExecutionControl& control,
            const CancellationToken& cancellation);

    private:
        IEventSink& m_eventSink;
    };
}

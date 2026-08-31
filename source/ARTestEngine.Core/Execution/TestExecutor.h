#pragma once

#include "ExecutionContext.h"
#include "ExecutionResult.h"
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

    private:
        IEventSink& m_eventSink;
    };
}

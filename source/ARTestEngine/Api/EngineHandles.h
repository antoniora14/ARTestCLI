#pragma once
#include "../../ARTestEngine.Core/Execution/ExecutionSession.h"
#include "EngineContext.h"
struct ARTestEngineOpaque
{
    std::unique_ptr<artest::engine::EngineContext> value;
};
struct ARTestCompiledPlanOpaque
{
    artest::engine::EngineContext *owner = nullptr;
    artest::TestPlan plan;
    std::vector<artest::CompiledStep> steps;
    std::uint64_t revision = 0;
};
struct ARTestSubscriptionOpaque
{
    artest::engine::EngineContext *owner = nullptr;
    std::uint64_t id = 0U;
};
struct ARTestSessionOpaque
{
    artest::engine::EngineContext *owner = nullptr;
    // Release the Engine lease only after the worker and all runtime instances.
    std::shared_ptr<int> runLease;
    std::unique_ptr<artest::InstrumentManager> instruments;
    std::unique_ptr<artest::IExecutionControl> control;
    std::unique_ptr<artest::ExecutionSession> execution;
    std::optional<artest::RunResult> result;
    std::mutex mutex;
};
struct ARTestResultOpaque
{
    artest::RunResult value;
};

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace artest
{
    enum class ExecutionDecision
    {
        Continue,
        Cancel
    };

    struct StepExecutionInfo
    {
        std::size_t commandIndex = 0;
        std::uint64_t stepId = 0;
        std::string commandName;
    };

    class IExecutionControl
    {
    public:
        virtual ~IExecutionControl() = default;
        [[nodiscard]] virtual ExecutionDecision BeforeStep(const StepExecutionInfo& step) = 0;
    };

    class RunToCompletionControl final : public IExecutionControl
    {
    public:
        [[nodiscard]] ExecutionDecision BeforeStep(const StepExecutionInfo&) override
        {
            return ExecutionDecision::Continue;
        }
    };
}

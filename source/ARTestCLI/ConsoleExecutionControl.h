#pragma once

#include "../ARTestEngine.Core/Execution/IExecutionControl.h"

#include <iosfwd>
#include <unordered_set>

namespace artest::cli
{
    class ConsoleExecutionControl final : public IExecutionControl
    {
    public:
        ConsoleExecutionControl(
            bool stepByStep,
            std::unordered_set<std::size_t> breakpoints,
            std::istream& input,
            std::ostream& output) noexcept;

        [[nodiscard]] ExecutionDecision BeforeStep(const StepExecutionInfo& step) override;

    private:
        bool m_stepByStep;
        std::unordered_set<std::size_t> m_breakpoints;
        std::istream& m_input;
        std::ostream& m_output;
    };
}

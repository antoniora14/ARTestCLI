#pragma once

#include "ARTestEngineClient.h"

#include <iosfwd>
#include <unordered_set>

namespace artest::cli
{
    class ConsoleExecutionControl final
    {
    public:
        ConsoleExecutionControl(
            bool stepByStep,
            std::unordered_set<std::size_t> breakpoints,
            std::istream& input,
            std::ostream& output) noexcept;

        [[nodiscard]] sdk::ExecutionDecision BeforeStep(
            const sdk::StepExecutionInfo& step);

    private:
        bool m_stepByStep;
        std::unordered_set<std::size_t> m_breakpoints;
        std::istream& m_input;
        std::ostream& m_output;
    };
}

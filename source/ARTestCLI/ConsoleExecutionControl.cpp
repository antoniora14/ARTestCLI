#include "ConsoleExecutionControl.h"

#include <istream>
#include <ostream>
#include <string>

namespace artest::cli
{
    ConsoleExecutionControl::ConsoleExecutionControl(
        bool stepByStep,
        std::unordered_set<std::size_t> breakpoints,
        std::istream& input,
        std::ostream& output) noexcept
        : m_stepByStep(stepByStep),
          m_breakpoints(std::move(breakpoints)),
          m_input(input),
          m_output(output)
    {
    }

    sdk::ExecutionDecision ConsoleExecutionControl::BeforeStep(
        const sdk::StepExecutionInfo& step)
    {
        if (!m_stepByStep && !m_breakpoints.contains(step.commandIndex))
        {
            return sdk::ExecutionDecision::Continue;
        }

        while (true)
        {
            m_output << "\n[Debug] Paused at step " << step.stepId << ": " << step.commandName << '\n'
                     << "Options: (n)ext, (c)ontinue, (q)uit > ";

            std::string answer;
            if (!std::getline(m_input, answer))
            {
                return sdk::ExecutionDecision::Cancel;
            }
            if (answer == "n" || answer == "N")
            {
                m_stepByStep = true;
                return sdk::ExecutionDecision::Continue;
            }
            if (answer == "c" || answer == "C")
            {
                m_stepByStep = false;
                return sdk::ExecutionDecision::Continue;
            }
            if (answer == "q" || answer == "Q")
            {
                return sdk::ExecutionDecision::Cancel;
            }
            m_output << "Invalid option. Enter 'n', 'c', or 'q'.\n";
        }
    }
}

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

    ExecutionDecision ConsoleExecutionControl::BeforeStep(const StepExecutionInfo& step)
    {
        if (!m_stepByStep && !m_breakpoints.contains(step.commandIndex))
        {
            return ExecutionDecision::Continue;
        }

        while (true)
        {
            m_output << "\n[Debug] Paused at step " << step.stepId << ": " << step.commandName << '\n'
                     << "Options: (n)ext, (c)ontinue, (q)uit > ";

            std::string answer;
            if (!std::getline(m_input, answer))
            {
                return ExecutionDecision::Cancel;
            }
            if (answer == "n" || answer == "N")
            {
                m_stepByStep = true;
                return ExecutionDecision::Continue;
            }
            if (answer == "c" || answer == "C")
            {
                m_stepByStep = false;
                return ExecutionDecision::Continue;
            }
            if (answer == "q" || answer == "Q")
            {
                return ExecutionDecision::Cancel;
            }
            m_output << "Invalid option. Enter 'n', 'c', or 'q'.\n";
        }
    }
}

#include "ScriptExecutor.h"
#include <iostream>
#include <limits>



CScriptExecutor::CScriptExecutor(std::vector<std::unique_ptr<ICommand>>&& commands) :
    m_commands(std::move(commands)){}


bool CScriptExecutor::Compile(std::vector<std::string>& errors) const
{
    errors.clear();
    for (size_t i = 0; i < m_commands.size(); ++i)
    {
        std::string error;
        if (!m_commands[i]->Validate(error))
        {
            errors.push_back("Step " + std::to_string(i) + ": " + error);
        }
    }
    return errors.empty();
}


void CScriptExecutor::Execute()
{
    ExecutionContext ExeContext;
    m_nDebugMode = m_bInteractiveMode ? DebugMode::StepByStep : DebugMode::None;

    for (size_t i = 0; i < m_commands.size(); ++i)
    {
        if (m_nDebugMode == DebugMode::Quit) break;

        bool shouldPause = (m_nDebugMode == DebugMode::StepByStep || m_breakpoints.find(i) != m_breakpoints.end());

        if (shouldPause) WaitForUser(i);

        std::cout << "|> Executing step " << i << ": " << m_commands[i]->Name() << "\n";
        m_commands[i]->Execute(ExeContext);
    }

    std::cout << "\nExecution finished.\n";
}


void CScriptExecutor::AddBreakpoint(size_t index)
{ m_breakpoints.insert(index); }


void CScriptExecutor::RemoveBreakpoint(size_t index)
{ m_breakpoints.erase(index); }


void CScriptExecutor::ClearBreakpoints()
{ m_breakpoints.clear(); }


void CScriptExecutor::SetInteractiveMode(bool bEnable)
{ m_bInteractiveMode = bEnable; }


void CScriptExecutor::WaitForUser(size_t stepIndex)
{
    while (true)
    {
        std::cout << "\n[Debug] Paused at step " << stepIndex << ": " << m_commands[stepIndex]->Name() << "\n";
        std::cout << "Options: (n)ext, (c)ontinue, (q)uit > ";

        std::string input;
        std::getline(std::cin, input);

        if (input == "n" || input == "N")
        {
            m_nDebugMode = DebugMode::StepByStep;
            break;
        }
        else if (input == "c" || input == "C")
        {
            m_nDebugMode = DebugMode::Continue;
            break;
        }
        else if (input == "q" || input == "Q")
        {
            m_nDebugMode = DebugMode::Quit;
            break;
        }
        else
        {
            std::cout << "Invalid option. Please enter 'n', 'c', or 'q'.\n";
        }
    }
}

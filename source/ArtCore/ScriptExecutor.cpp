#include "ScriptExecutor.h"
#include <iostream>
#include <limits>



CScriptExecutor::CScriptExecutor(std::vector<CommandInstance>&& commands) :
    m_commands(std::move(commands)){}


OperationResult CScriptExecutor::Compile() const
{
    OperationResult result;
    for (size_t i = 0; i < m_commands.size(); ++i)
    {
        std::string error;
        if (!m_commands[i].command->Validate(error))
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "COMMAND_VALIDATION_FAILED",
                error,
                "stepId=" + std::to_string(m_commands[i].stepId)});
        }
    }
    return result;
}


RunResult CScriptExecutor::Execute()
{
    ExecutionContext ExeContext;
    RunResult run;
    m_nDebugMode = m_bInteractiveMode ? DebugMode::StepByStep : DebugMode::None;

    for (size_t i = 0; i < m_commands.size(); ++i)
    {
        if (m_nDebugMode == DebugMode::Quit) break;

        bool shouldPause = (m_nDebugMode == DebugMode::StepByStep || m_breakpoints.find(i) != m_breakpoints.end());

        if (shouldPause)
        {
            WaitForUser(i);
            if (m_nDebugMode == DebugMode::Quit)
            {
                run.status = RunStatus::Cancelled;
                break;
            }
        }

        const auto start = std::chrono::steady_clock::now();
        StepResult stepResult;
        try
        {
            std::cout << "|> Executing step " << m_commands[i].stepId << ": " << m_commands[i].command->Name() << "\n";
            stepResult = m_commands[i].command->Execute(ExeContext);
        }
        catch (const std::exception& exception)
        {
            stepResult = StepResult::Error(exception.what());
        }
        catch (...)
        {
            stepResult = StepResult::Error("Unknown exception while executing the step.");
        }

        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        run.steps.push_back({m_commands[i].stepId, m_commands[i].command->Name(), stepResult, duration});
        if (!stepResult.Succeeded())
        {
            run.status = stepResult.status == StepStatus::Failed ? RunStatus::Failed : RunStatus::Error;
            std::cerr << "Step " << m_commands[i].stepId << " failed: " << stepResult.message << "\n";
            break;
        }
    }

    std::cout << "\nExecution finished with "
        << (run.Succeeded() ? "PASSED" : "FAILED") << ".\n";
    return run;
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
        std::cout << "\n[Debug] Paused at step " << m_commands[stepIndex].stepId << ": " << m_commands[stepIndex].command->Name() << "\n";
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

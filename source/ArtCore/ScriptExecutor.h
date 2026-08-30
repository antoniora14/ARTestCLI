#pragma once

#include "ICommands.h"
#include "ExecutionContext.h"
#include "Diagnostics.h"
#include "ExecutionResult.h"
#include <vector>
#include <memory>
#include <unordered_set>
#include <string>


enum class DebugMode
{
    None,
    StepByStep,
    Continue,
    Quit
};

class CScriptExecutor
{
public:
	explicit CScriptExecutor(std::vector<CommandInstance>&& commands);

    [[nodiscard]] OperationResult Compile() const;
    [[nodiscard]] RunResult Execute();

    void AddBreakpoint(size_t index);
    void RemoveBreakpoint(size_t index);
    void ClearBreakpoints();

    void SetInteractiveMode(bool bEnable);

private:
    std::vector<CommandInstance> m_commands;
    std::unordered_set<size_t> m_breakpoints;

    bool m_bInteractiveMode = false;
    DebugMode m_nDebugMode = DebugMode::StepByStep;

    void WaitForUser(size_t stepIndex);
};


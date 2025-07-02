#pragma once
#include <string>
#include <unordered_map>


class ExecutionContext
{
private:
    std::unordered_map<std::string, int> m_variables;

public:
    bool m_bBreakFlag = false;
    bool m_bContinueFlag = false;
    bool m_bInteractiveMode = false;

    void SetVar(const std::string& name, int value);
    int GetVar(const std::string& name) const;
    void IncVar(const std::string& name);
    bool EvaluateCondition(const std::string& expr) const;
};


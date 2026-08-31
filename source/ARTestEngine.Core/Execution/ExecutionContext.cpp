#include "ExecutionContext.h"

#include <regex>
#include <stdexcept>

namespace artest
{
    void ExecutionContext::SetVariable(const std::string& name, int value)
    {
        m_variables[name] = value;
    }

    int ExecutionContext::GetVariable(const std::string& name) const
    {
        const auto item = m_variables.find(name);
        if (item != m_variables.end())
        {
            return item->second;
        }
        throw std::runtime_error("Variable not found: " + name);
    }

    void ExecutionContext::IncrementVariable(const std::string& name)
    {
        ++m_variables[name];
    }

    bool ExecutionContext::EvaluateCondition(const std::string& expression) const
    {
        const std::regex pattern(R"((\w+)\s*(==|!=|<=|>=|<|>)\s*(-?\d+))");
        std::smatch match;
        if (!std::regex_match(expression, match, pattern))
        {
            return false;
        }

        const std::string variableName = match[1];
        const std::string operation = match[2];
        const int rightHandSide = std::stoi(match[3]);
        const int leftHandSide = GetVariable(variableName);

        if (operation == "==") return leftHandSide == rightHandSide;
        if (operation == "!=") return leftHandSide != rightHandSide;
        if (operation == "<") return leftHandSide < rightHandSide;
        if (operation == ">") return leftHandSide > rightHandSide;
        if (operation == "<=") return leftHandSide <= rightHandSide;
        if (operation == ">=") return leftHandSide >= rightHandSide;
        return false;
    }
}

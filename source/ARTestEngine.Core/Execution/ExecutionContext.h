#pragma once

#include <string>
#include <unordered_map>

namespace artest
{
    class ExecutionContext
    {
    public:
        void SetVariable(const std::string& name, int value);
        [[nodiscard]] int GetVariable(const std::string& name) const;
        void IncrementVariable(const std::string& name);
        [[nodiscard]] bool EvaluateCondition(const std::string& expression) const;

    private:
        std::unordered_map<std::string, int> m_variables;
    };
}

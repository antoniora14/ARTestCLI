#include "ExecutionContext.h"
#include <regex>
#include <stdexcept>


void ExecutionContext::SetVar(const std::string& name, int value)
{
	m_variables[name] = value;
}

int ExecutionContext::GetVar(const std::string& name) const
{
	auto item = m_variables.find(name);
	if (item != m_variables.end()) return item->second;
	throw std::runtime_error("Variable not found: " + name);
}

void ExecutionContext::IncVar(const std::string& name)
{
	m_variables[name]++;
}

bool ExecutionContext::EvaluateCondition(const std::string& expr) const
{
    std::regex pattern(R"((\w+)\s*(==|!=|<=|>=|<|>)\s*(\d+))");
    std::smatch match;
    if (std::regex_match(expr, match, pattern))
    {
        std::string varName = match[1];
        std::string op = match[2];
        int rhs = std::stoi(match[3]);

        int lhs = GetVar(varName);

        if (op == "==") return lhs == rhs;
        if (op == "!=") return lhs != rhs;
        if (op == "<")  return lhs < rhs;
        if (op == ">")  return lhs > rhs;
        if (op == "<=") return lhs <= rhs;
        if (op == ">=") return lhs >= rhs;
    }
    return false;
}

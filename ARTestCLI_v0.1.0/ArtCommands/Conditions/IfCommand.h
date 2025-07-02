#pragma once
#include "../../ArtCore/ICommands.h"
#include <string>
#include <vector>
#include <memory>

#define CMD_CONDITION_IF_NAME       "IF"


class IfCommand : public ICommand
{
private:
    std::string m_condition;
    std::vector<std::unique_ptr<ICommand>> m_body;

public:
    virtual std::string Name() const override { return CMD_CONDITION_IF_NAME; }
    virtual bool Validate(std::string& error) override { return true; }
    virtual void Execute(ExecutionContext& context) override;
    virtual void Configure(const nlohmann::json& json) override;
};


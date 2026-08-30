#include "IfCommand.h"
#include "../../ArtCore/CCommandFactory.h"
#include "../../ArtCore/RegisterCommand.h"


REGISTER_COMMAND(CMD_CONDITION_IF_NAME, IfCommand)


bool IfCommand::Validate(std::string& error) const
{
    error = "IF is reserved but is not implemented in script version 1.";
    return false;
}

StepResult IfCommand::Execute(ExecutionContext& /*context*/)
{
    return StepResult::Error("IF execution is not implemented.");
}

void IfCommand::Configure(const nlohmann::json& json, std::shared_ptr<IInstrument> instrument)
{
    (void)instrument;
    m_condition = json.value("condition", std::string{});
}

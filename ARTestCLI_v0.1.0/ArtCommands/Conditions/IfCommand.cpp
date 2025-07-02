#include "IfCommand.h"
#include "../../ArtCore/CCommandFactory.h"
#include "../../ArtCore/RegisterCommand.h"


REGISTER_COMMAND(CMD_CONDITION_IF_NAME, IfCommand)


void IfCommand::Execute(ExecutionContext& /*context*/)
{
}

void IfCommand::Configure(const nlohmann::json& json)
{
    m_condition = json["params"]["condition"];

    if (json.contains("body"))
    {
        //for (const auto& item : json["body"])
        //{
        //    auto cmd = CommandFactory::CreateCommand(item["name"]);
        //    if (item.contains("params"))
        //        cmd->FromJson(item["params"]);
        //    m_body.push_back(std::move(cmd));
        //}
    }
}

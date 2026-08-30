#include "ArtPowerOffCommand.h"
#include "../../ArtCore/RegisterCommand.h"
#include "../../ArtInstruments/PowerSupply.h"
#include <iostream>

REGISTER_COMMAND(CMD_PS_TURNOFF_NAME, PowerOffCommand)


std::string PowerOffCommand::Name() const
{
    return CMD_PS_TURNOFF_NAME;
}

bool PowerOffCommand::Validate(std::string& csError) const
{
    if (m_nChannel < 0)
    {
        csError = "Invalid channel value.";
        return false;
    }
    if (!std::dynamic_pointer_cast<PowerSupply>(m_PowerSupplyInstr))
    {
        csError = "The command requires a PowerSupply instrument.";
        return false;
    }
    return true;
}

StepResult PowerOffCommand::Execute(ExecutionContext& /*context*/)
{
    auto PS = std::dynamic_pointer_cast<PowerSupply>(m_PowerSupplyInstr);
    if (!PS)
    {
        return StepResult::Error("The bound instrument is not a PowerSupply.");
    }

    OperationResult result = PS->TurnOff(m_nChannel);
    if (!result.Succeeded())
    {
        return StepResult::Error(result.diagnostics.front().message);
    }
    return StepResult::Pass();
}

void PowerOffCommand::Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument)
{
    m_PowerSupplyInstr = spInstrument;
    m_nChannel = params.value("channel", -1);
}

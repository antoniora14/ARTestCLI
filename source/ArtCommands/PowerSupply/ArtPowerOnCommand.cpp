#include "ArtPowerOnCommand.h"
#include "../../ArtCore/RegisterCommand.h"
#include "../../ArtInstruments/PowerSupply.h"
#include <iostream>

REGISTER_COMMAND(CMD_PS_TURNON_NAME, PowerOnCommand)


std::string PowerOnCommand::Name() const
{
    return CMD_PS_TURNON_NAME;
}

bool PowerOnCommand::Validate(std::string& csError) const
{
    if (m_nChannel < 0)
    {
        csError = "Invalid channel value.";
        return false;
    }
    if (m_fVoltage < 0.0)
    {
        csError = "Voltage must be zero or greater.";
        return false;
    }
    if (m_fCurrent < 0.0)
    {
        csError = "Current limit must be zero or greater.";
        return false;
    }
    if (!std::dynamic_pointer_cast<PowerSupply>(m_PowerSupplyInstr))
    {
        csError = "The command requires a PowerSupply instrument.";
        return false;
    }
    return true;
}

StepResult PowerOnCommand::Execute(ExecutionContext& /*context*/)
{
    auto PS = std::dynamic_pointer_cast<PowerSupply>(m_PowerSupplyInstr);
    if (!PS)
    {
        return StepResult::Error("The bound instrument is not a PowerSupply.");
    }

    OperationResult result = PS->SetVoltage(m_nChannel, m_fVoltage);
    if (!result.Succeeded())
    {
        return StepResult::Error(result.diagnostics.front().message);
    }
    result = PS->SetCurrent(m_nChannel, m_fCurrent);
    if (!result.Succeeded())
    {
        return StepResult::Error(result.diagnostics.front().message);
    }
    result = PS->TurnOn(m_nChannel);
    if (!result.Succeeded())
    {
        return StepResult::Error(result.diagnostics.front().message);
    }
    return StepResult::Pass();
}

void PowerOnCommand::Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument)
{
    m_PowerSupplyInstr = spInstrument;
    m_nChannel = params.value("channel", -1);
    m_fVoltage = params.value("voltage", -1.0);
    m_fCurrent = params.value("currentLimit", -1.0);
}

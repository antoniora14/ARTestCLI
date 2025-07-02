#include "ArtPowerOnCommand.h"
#include "../../ArtCore/RegisterCommand.h"
#include "../../ArtInstruments/PowerSupply.h"
#include <iostream>

REGISTER_COMMAND(CMD_PS_TURNON_NAME, PowerOnCommand)


std::string PowerOnCommand::Name() const
{
    return CMD_PS_TURNON_NAME;
}

bool PowerOnCommand::Validate(std::string& csError)
{
    if (m_nChannel < 0)
    {
        csError = "Invalid channel value.";
        return false;
    }
    return true;
}

void PowerOnCommand::Execute(ExecutionContext& /*context*/)
{
    auto PS = std::dynamic_pointer_cast<PowerSupply>(m_PowerSupplyInstr);
    if (PS)
    {
        PS->SetVoltage(m_fVoltage);
        PS->SetCurrent(m_fCurrent);
        PS->TurnOn(m_nChannel);
        std::cout << "[PowerSupplyTurnOnCommand] Ejecutado correctamente." << std::endl;
    }
    else
    {
        std::cerr << "[PowerSupplyTurnOnCommand] Error: instrumento no es PowerSupply." << std::endl;
    }
}

void PowerOnCommand::Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument)
{
    m_PowerSupplyInstr = spInstrument;
    m_nChannel = params.value("channel", 0);
    m_fVoltage = params.value("voltage", 0.0);
    m_fCurrent = params.value("currentLimit", 0.0);
}

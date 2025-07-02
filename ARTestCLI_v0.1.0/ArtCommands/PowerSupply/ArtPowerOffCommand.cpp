#include "ArtPowerOffCommand.h"
#include "../../ArtCore/RegisterCommand.h"
#include "../../ArtInstruments/PowerSupply.h"
#include <iostream>

REGISTER_COMMAND(CMD_PS_TURNOFF_NAME, PowerOffCommand)


std::string PowerOffCommand::Name() const
{
    return CMD_PS_TURNOFF_NAME;
}

bool PowerOffCommand::Validate(std::string& csError)
{
    if (m_nChannel < 0)
    {
        csError = "Invalid channel value.";
        return false;
    }
    return true;
}

void PowerOffCommand::Execute(ExecutionContext& /*context*/)
{
    auto PS = std::dynamic_pointer_cast<PowerSupply>(m_PowerSupplyInstr);
    if (PS)
    {
        PS->TurnOff(m_nChannel);
        std::cout << "[PowerSupplyTurnOffCommand] Ejecutado correctamente." << std::endl;
    }
    else
    {
        std::cerr << "[PowerSupplyTurnOffCommand] Error: instrumento no es PowerSupply." << std::endl;
    }
}

void PowerOffCommand::Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument)
{
    m_PowerSupplyInstr = spInstrument;
    m_nChannel = params.value("channel", 0);
}

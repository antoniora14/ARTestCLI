#include "PowerSupply.h"
#include "RegisterInstrument.h"
#include <iostream>

REGISTER_INSTRUMENT(INSTRUMENT_POWERSUPPLY_NAME, PowerSupply)


void PowerSupply::Initialize(const nlohmann::json& params)
{
	std::cout << "[PowerSupply] Initialize" << std::endl;
}

void PowerSupply::Configure(std::string hw_rsrc)
{
	std::cout << "[PowerSupply] Configure" << std::endl;
}

void PowerSupply::TurnOn(int nChannel)
{
	std::cout << "[PowerSupply] TurnOn" << std::endl;
}

void PowerSupply::TurnOff(int nChannel)
{
	std::cout << "[PowerSupply] TurnOFF" << std::endl;
}

void PowerSupply::SetVoltage(double voltage)
{
	std::cout << "[PowerSupply] SetVoltage" << std::endl;
}

void PowerSupply::SetCurrent(double current)
{
	std::cout << "[PowerSupply] SetCurrent" << std::endl;
}

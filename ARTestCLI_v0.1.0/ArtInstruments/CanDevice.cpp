#include "CanDevice.h"
#include "RegisterInstrument.h"
#include <iostream>

REGISTER_INSTRUMENT(INSTRUMENT_CAN_NAME, CANDevice)

void CANDevice::Initialize(const nlohmann::json& params)
{
	std::cout << "[CANDevice] Initialize" << std::endl;
}

void CANDevice::Configure(std::string hw_rsrc)
{
	std::cout << "[CANDevice] Configure" << std::endl;
}

void CANDevice::SendMessage()
{
	std::cout << "[CANDevice] SendMessage" << std::endl;
}

void CANDevice::ReceiveMessage()
{
	std::cout << "[CANDevice] ReceiveMessage" << std::endl;
}

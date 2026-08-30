#include "PowerSupply.h"
#include "RegisterInstrument.h"
#include <iostream>

REGISTER_INSTRUMENT(INSTRUMENT_POWERSUPPLY_NAME, PowerSupply)


OperationResult PowerSupply::Initialize(const nlohmann::json& params)
{
	const std::string resource = params.value("hw-rsrc", std::string{});
	if (resource.empty())
	{
		return OperationResult::Failure("POWER_SUPPLY_RESOURCE_MISSING", "The hw-rsrc parameter is required.");
	}
	std::cout << "[PowerSupply:" << m_sPSId << "] Initialize " << resource << std::endl;
	return OperationResult::Success();
}

void PowerSupply::Shutdown() noexcept
{
	std::cout << "[PowerSupply:" << m_sPSId << "] Shutdown" << std::endl;
}

OperationResult PowerSupply::TurnOn(int channel)
{
	std::cout << "[PowerSupply:" << m_sPSId << "] TurnOn channel " << channel << std::endl;
	return OperationResult::Success();
}

OperationResult PowerSupply::TurnOff(int channel)
{
	std::cout << "[PowerSupply:" << m_sPSId << "] TurnOff channel " << channel << std::endl;
	return OperationResult::Success();
}

OperationResult PowerSupply::SetVoltage(int channel, double voltage)
{
	std::cout << "[PowerSupply:" << m_sPSId << "] SetVoltage channel " << channel << " = " << voltage << std::endl;
	return OperationResult::Success();
}

OperationResult PowerSupply::SetCurrent(int channel, double current)
{
	std::cout << "[PowerSupply:" << m_sPSId << "] SetCurrent channel " << channel << " = " << current << std::endl;
	return OperationResult::Success();
}

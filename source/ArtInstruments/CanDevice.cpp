#include "CanDevice.h"
#include "RegisterInstrument.h"
#include <iostream>

REGISTER_INSTRUMENT(INSTRUMENT_CAN_NAME, CANDevice)

OperationResult CANDevice::Initialize(const nlohmann::json& params)
{
	const std::string resource = params.value("hw-rsrc", std::string{});
	if (resource.empty())
	{
		return OperationResult::Failure("CAN_RESOURCE_MISSING", "The hw-rsrc parameter is required.");
	}
	std::cout << "[CAN:" << m_sCanId << "] Initialize " << resource << std::endl;
	return OperationResult::Success();
}

void CANDevice::Shutdown() noexcept
{
	std::cout << "[CAN:" << m_sCanId << "] Shutdown" << std::endl;
}

OperationResult CANDevice::SendMessage(
	int channel,
	std::uint32_t messageId,
	const std::vector<std::uint8_t>& data)
{
	std::cout << "[CAN:" << m_sCanId << "] SendMessage channel=" << channel
		<< " id=" << messageId << " bytes=" << data.size() << std::endl;
	return OperationResult::Success();
}

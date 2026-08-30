#pragma once
#include "IInstrument.h"
#include <cstdint>
#include <vector>

#define INSTRUMENT_CAN_NAME       "CAN"


class CANDevice : public IInstrument
{
private:
	std::string m_sCanId = "";

public:
	CANDevice() = default;

	virtual std::string GetId() const override { return m_sCanId; }
	virtual void SetId(std::string id) override { m_sCanId = std::move(id); }
	[[nodiscard]] virtual OperationResult Initialize(const nlohmann::json& params) override;
	virtual void Shutdown() noexcept override;

	[[nodiscard]] OperationResult SendMessage(
		int channel,
		std::uint32_t messageId,
		const std::vector<std::uint8_t>& data);
};

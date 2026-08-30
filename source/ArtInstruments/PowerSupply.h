#pragma once
#include "IInstrument.h"

#define INSTRUMENT_POWERSUPPLY_NAME       "PowerSupply"


class PowerSupply : public IInstrument
{
private:
    std::string m_sPSId = "";
    int m_nPSchannel;

public:
    PowerSupply() = default;
    PowerSupply(const std::string& ID) : m_sPSId(ID) {}

	virtual std::string GetId() const override { return m_sPSId; }
	virtual void SetId(std::string id) override { m_sPSId = std::move(id); }
	[[nodiscard]] virtual OperationResult Initialize(const nlohmann::json& params) override;
	virtual void Shutdown() noexcept override;

	[[nodiscard]] OperationResult TurnOn(int channel);
	[[nodiscard]] OperationResult TurnOff(int channel);
	[[nodiscard]] OperationResult SetVoltage(int channel, double voltage);
	[[nodiscard]] OperationResult SetCurrent(int channel, double current);
};

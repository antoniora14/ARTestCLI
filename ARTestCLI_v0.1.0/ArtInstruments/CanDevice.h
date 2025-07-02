#pragma once
#include "IInstrument.h"

#define INSTRUMENT_CAN_NAME       "CAN"


class CANDevice : public IInstrument
{
private:
	std::string m_sCanId = "";

public:
	CANDevice() = default;

	virtual std::string GetId() const override { return m_sCanId; }
	virtual void Initialize(const nlohmann::json& params) override;

	void Configure(std::string hw_rsrc);
	void SendMessage();
	void ReceiveMessage();
};


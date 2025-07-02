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
    virtual void Initialize(const nlohmann::json& params) override;

    void Configure(std::string hw_rsrc);
    void TurnOn(int nChannel);
    void TurnOff(int nChannel);
    void SetVoltage(double voltage);
    void SetCurrent(double current);
};


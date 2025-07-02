#pragma once
#include "../../ArtCore/ICommands.h"

#define CMD_PS_TURNON_NAME       "PowerSupply.TurnOn"


class PowerOnCommand : public ICommand
{
private:
    std::shared_ptr<IInstrument> m_PowerSupplyInstr;
    int             m_nChannel;
    float           m_fVoltage;
    float           m_fCurrent;

public:
    PowerOnCommand() = default;
    PowerOnCommand(int channel, float voltage, float current) 
        : m_nChannel(channel), m_fVoltage(voltage), m_fCurrent(current) {}

    virtual std::string Name() const override;
    virtual bool Validate(std::string& csError) override;
    virtual void Execute(ExecutionContext& context) override;
    void Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument) override;
};


#pragma once
#include "../../ArtCore/ICommands.h"

#define CMD_PS_TURNOFF_NAME       "PowerSupply.TurnOff"


class PowerOffCommand : public ICommand
{
private:
    std::shared_ptr<IInstrument> m_PowerSupplyInstr;
    int m_nChannel = 0;

public:
    PowerOffCommand() = default;
    PowerOffCommand(int channel) : m_nChannel(channel) {}

    virtual std::string Name() const override;
    virtual bool Validate(std::string& csError) override;
    virtual void Execute(ExecutionContext& context) override;
    virtual void Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument) override;
};
#pragma once
#include "../../../ArtCore/ICommands.h"
#include <string>
#include <vector>
#include <cstdint>


#define CMD_CAN_SENDMESSAGE_NAME	"CAN.SendMessage"


class SendCanMsgCommand : public ICommand
{
private:
    std::shared_ptr<IInstrument> m_CANInstr;

    int m_channel = -1;
    std::uint32_t m_Id = 0;
    int m_dlc = -1;
    std::vector<std::uint8_t> m_data;
	
public:
    SendCanMsgCommand() = default;

    virtual std::string Name() const override;
    virtual bool Validate(std::string & csError) const override;
    virtual StepResult Execute(ExecutionContext & context) override;
    void Configure(const nlohmann::json & params, std::shared_ptr<IInstrument> spInstrument) override;
};


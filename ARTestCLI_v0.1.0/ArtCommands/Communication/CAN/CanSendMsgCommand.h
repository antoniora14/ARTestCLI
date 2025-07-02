#pragma once
#include "../../../ArtCore/ICommands.h"
#include <string>
#include <vector>


#define CMD_CAN_SENDMESSAGE_NAME	"CAN.SendMessage"


class SendCanMsgCommand : public ICommand
{
private:
    std::shared_ptr<IInstrument> m_CANInstr;

    int m_channel;
    int m_Id;
    int m_dlc;
    std::vector<uint8_t> m_data;
	
public:
    SendCanMsgCommand() = default;

    virtual std::string Name() const override;
    virtual bool Validate(std::string & csError) override;
    virtual void Execute(ExecutionContext & context) override;
    void Configure(const nlohmann::json & params, std::shared_ptr<IInstrument> spInstrument) override;
};


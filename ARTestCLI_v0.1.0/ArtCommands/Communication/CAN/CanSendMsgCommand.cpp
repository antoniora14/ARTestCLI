#include "CanSendMsgCommand.h"
#include "../../../ArtCore/RegisterCommand.h"
#include "../../../ArtInstruments/CanDevice.h"
#include <iostream>

REGISTER_COMMAND(CMD_CAN_SENDMESSAGE_NAME, SendCanMsgCommand)


std::string SendCanMsgCommand::Name() const
{
	return CMD_CAN_SENDMESSAGE_NAME;
}

bool SendCanMsgCommand::Validate(std::string& csError)
{
    if (m_channel < 0) 
    {
        csError = "Invalid channel.";
        return false;
    }
    if (m_dlc < 0 || m_dlc > 8)
    {
        csError = "DLC must be between 0 and 8.";
        return false;
    }
    if (m_data.size() != static_cast<size_t>(m_dlc)) 
    {
        csError = "Data length does not match DLC.";
        return false;
    }
    return true;
}

void SendCanMsgCommand::Execute(ExecutionContext& context)
{
    auto CAN = std::dynamic_pointer_cast<CANDevice>(m_CANInstr);
    if (CAN)
    {
        CAN->SendMessage();
        std::cout << "[CAN] Channel: " << m_channel
            << ", ID: 0x" << std::hex << std::uppercase << m_Id
            << ", DLC: " << std::dec << m_dlc
            << ", Data: ";

        for (uint8_t byte : m_data)
        {
            std::cout << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte) << " ";
        }
        std::cout << std::dec << "\n";
    }
    else
    {
        std::cerr << "[SendCanMsgCommand] Error: instrumento no es PowerSupply." << std::endl;
    }
    
}

void SendCanMsgCommand::Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument)
{
    m_CANInstr = spInstrument;
    if (params.contains("channel")) m_channel = params["channel"];
    if (params.contains("id"))
    {
        std::string idStr = params["id"];
        m_Id = std::stoul(idStr, nullptr, 0);
    }
    if (params.contains("dlc")) m_dlc = params["dlc"];
    if (params.contains("data") && params["data"].is_array())
    {
        m_data.clear();
        for (const auto& data : params["data"])
        {
            m_data.push_back(static_cast<uint8_t>(data));
        }
    }
}

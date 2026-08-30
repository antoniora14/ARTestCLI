#include "CanSendMsgCommand.h"
#include "../../../ArtCore/RegisterCommand.h"
#include "../../../ArtInstruments/CanDevice.h"
#include <iostream>

REGISTER_COMMAND(CMD_CAN_SENDMESSAGE_NAME, SendCanMsgCommand)


std::string SendCanMsgCommand::Name() const
{
	return CMD_CAN_SENDMESSAGE_NAME;
}

bool SendCanMsgCommand::Validate(std::string& csError) const
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
    if (m_Id > 0x1FFFFFFFU)
    {
        csError = "CAN identifier exceeds the 29-bit limit.";
        return false;
    }
    if (!std::dynamic_pointer_cast<CANDevice>(m_CANInstr))
    {
        csError = "The command requires a CAN instrument.";
        return false;
    }
    return true;
}

StepResult SendCanMsgCommand::Execute(ExecutionContext& /*context*/)
{
    auto CAN = std::dynamic_pointer_cast<CANDevice>(m_CANInstr);
    if (!CAN)
    {
        return StepResult::Error("The bound instrument is not a CAN device.");
    }

    OperationResult result = CAN->SendMessage(m_channel, m_Id, m_data);
    if (!result.Succeeded())
    {
        return StepResult::Error(result.diagnostics.front().message);
    }
    return StepResult::Pass();
}

void SendCanMsgCommand::Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument)
{
    m_CANInstr = spInstrument;
    if (params.contains("channel")) m_channel = params["channel"];
    if (params.contains("id"))
    {
        std::string idStr = params["id"];
        m_Id = static_cast<std::uint32_t>(std::stoul(idStr, nullptr, 0));
    }
    if (params.contains("dlc")) m_dlc = params["dlc"];
    if (params.contains("data") && params["data"].is_array())
    {
        m_data.clear();
        for (const auto& data : params["data"])
        {
            const int value = data.get<int>();
            if (value < 0 || value > 255)
            {
                throw std::out_of_range("CAN data bytes must be between 0 and 255.");
            }
            m_data.push_back(static_cast<std::uint8_t>(value));
        }
    }
}

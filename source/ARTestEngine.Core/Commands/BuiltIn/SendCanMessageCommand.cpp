#include "SendCanMessageCommand.h"

#include <limits>

namespace artest
{
    std::string SendCanMessageCommand::Name() const
    {
        return SendCanMessageCommandName;
    }

    OperationResult SendCanMessageCommand::Configure(
        const nlohmann::json& parameters,
        std::shared_ptr<IInstrument> instrument)
    {
        m_canDevice = std::dynamic_pointer_cast<ICanDevice>(std::move(instrument));
        m_data.clear();
        m_configurationError.clear();

        try
        {
            m_channel = parameters.value("channel", -1);
            m_dlc = parameters.value("dlc", -1);

            if (!parameters.contains("id"))
            {
                m_configurationError = "The id parameter is required.";
            }
            else if (parameters["id"].is_string())
            {
                const auto id = parameters["id"].get<std::string>();
                std::size_t parsedCharacters = 0;
                const auto parsed = std::stoull(id, &parsedCharacters, 0);
                if (parsedCharacters != id.size() || parsed > std::numeric_limits<std::uint32_t>::max())
                {
                    m_configurationError = "The CAN identifier is invalid.";
                }
                else
                {
                    m_messageId = static_cast<std::uint32_t>(parsed);
                }
            }
            else if (parameters["id"].is_number_unsigned())
            {
                m_messageId = parameters["id"].get<std::uint32_t>();
            }
            else
            {
                m_configurationError = "The CAN identifier must be an unsigned integer or a numeric string.";
            }

            if (parameters.contains("data") && parameters["data"].is_array())
            {
                for (const auto& byte : parameters["data"])
                {
                    if (!byte.is_number_integer())
                    {
                        m_configurationError = "Every CAN data element must be an integer byte.";
                        break;
                    }
                    const auto value = byte.get<int>();
                    if (value < 0 || value > 255)
                    {
                        m_configurationError = "CAN data bytes must be between 0 and 255.";
                        break;
                    }
                    m_data.push_back(static_cast<std::uint8_t>(value));
                }
            }
            else
            {
                m_configurationError = "The data parameter must be an array.";
            }
        }
        catch (const std::exception& exception)
        {
            m_configurationError = exception.what();
        }

        if (!m_configurationError.empty())
        {
            return OperationResult::Failure("CAN_CONFIGURATION_INVALID", m_configurationError);
        }
        return OperationResult::Success();
    }

    OperationResult SendCanMessageCommand::Validate() const
    {
        if (!m_canDevice)
        {
            return OperationResult::Failure("CAN_INSTRUMENT_REQUIRED", "The command requires a CAN instrument.");
        }
        if (!m_configurationError.empty())
        {
            return OperationResult::Failure("CAN_CONFIGURATION_INVALID", m_configurationError);
        }
        if (m_channel < 0)
        {
            return OperationResult::Failure("CAN_CHANNEL_INVALID", "The channel must be zero or greater.");
        }
        if (m_dlc < 0 || m_dlc > 8)
        {
            return OperationResult::Failure("CAN_DLC_INVALID", "The DLC must be between 0 and 8.");
        }
        if (m_data.size() != static_cast<std::size_t>(m_dlc))
        {
            return OperationResult::Failure("CAN_DATA_LENGTH_INVALID", "The data length does not match the DLC.");
        }
        if (m_messageId > 0x1FFFFFFFU)
        {
            return OperationResult::Failure("CAN_ID_INVALID", "The CAN identifier exceeds the 29-bit limit.");
        }
        return OperationResult::Success();
    }

    StepResult SendCanMessageCommand::Execute(
        ExecutionContext&,
        const CancellationToken& cancellation)
    {
        if (cancellation.Reason() != CancellationReason::None)
        {
            return cancellation.IsTimedOut() ? StepResult::Timeout() : StepResult::Cancel();
        }
        if (!m_canDevice)
        {
            return StepResult::Error("The bound instrument is not a CAN device.");
        }
        const auto result = m_canDevice->SendMessage(m_channel, m_messageId, m_data);
        if (!result.Succeeded())
        {
            return StepResult::Error(
                result.diagnostics.empty() ? "The instrument operation failed." : result.diagnostics.front().message);
        }
        return StepResult::Pass();
    }
}

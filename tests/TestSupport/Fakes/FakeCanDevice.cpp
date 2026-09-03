#include "FakeCanDevice.h"

// Test-only driver double; production simulation is supplied by native packages.

#include <sstream>

namespace artest
{
    FakeCanDevice::FakeCanDevice(IEventSink& eventSink) noexcept
        : m_eventSink(eventSink)
    {
    }

    std::string FakeCanDevice::GetId() const
    {
        return m_id;
    }

    void FakeCanDevice::SetId(std::string id)
    {
        m_id = std::move(id);
    }

    OperationResult FakeCanDevice::Initialize(const nlohmann::json& configuration)
    {
        try
        {
            const auto resource = configuration.value("hw-rsrc", std::string{});
            if (resource.empty())
            {
                return OperationResult::Failure("CAN_RESOURCE_MISSING", "The hw-rsrc parameter is required.");
            }
            if (configuration.value("failInitialization", false))
            {
                return OperationResult::Failure(
                    "CAN_INITIALIZATION_FORCED_FAILURE",
                    "Fake CAN initialization was configured to fail.");
            }
            m_initialized = true;
            Publish("[CAN:" + m_id + "] Initialize " + resource);
            return OperationResult::Success();
        }
        catch (const std::exception& exception)
        {
            return OperationResult::Failure("CAN_CONFIGURATION_INVALID", exception.what());
        }
    }

    OperationResult FakeCanDevice::Shutdown()
    {
        if (m_initialized)
        {
            Publish("[CAN:" + m_id + "] Shutdown");
        }
        m_initialized = false;
        return OperationResult::Success();
    }

    OperationResult FakeCanDevice::SendMessage(
        int channel,
        std::uint32_t messageId,
        const std::vector<std::uint8_t>& data)
    {
        if (!m_initialized)
        {
            return OperationResult::Failure("CAN_NOT_INITIALIZED", "The CAN instrument is not initialized.");
        }
        if (channel < 0)
        {
            return OperationResult::Failure("CAN_CHANNEL_INVALID", "The channel must be zero or greater.");
        }
        if (messageId > 0x1FFFFFFFU)
        {
            return OperationResult::Failure("CAN_ID_INVALID", "The CAN identifier exceeds the 29-bit limit.");
        }
        if (data.size() > 8)
        {
            return OperationResult::Failure("CAN_DATA_LENGTH_INVALID", "Classic CAN data cannot exceed eight bytes.");
        }

        m_messages.push_back({channel, messageId, data});
        std::ostringstream text;
        text << "[CAN:" << m_id << "] SendMessage channel=" << channel
             << " id=" << messageId << " bytes=" << data.size();
        Publish(text.str());
        return OperationResult::Success();
    }

    bool FakeCanDevice::IsInitialized() const noexcept
    {
        return m_initialized;
    }

    const std::vector<SentCanMessage>& FakeCanDevice::SentMessages() const noexcept
    {
        return m_messages;
    }

    void FakeCanDevice::Publish(std::string message) noexcept
    {
        m_eventSink.Publish({
            EngineEventKind::InstrumentOperation,
            EngineEventSeverity::Information,
            m_id,
            std::move(message)});
    }
}

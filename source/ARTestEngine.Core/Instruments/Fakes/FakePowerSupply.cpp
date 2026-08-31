#include "FakePowerSupply.h"

#include <sstream>

namespace artest
{
    FakePowerSupply::FakePowerSupply(IEventSink& eventSink) noexcept
        : m_eventSink(eventSink)
    {
    }

    std::string FakePowerSupply::GetId() const
    {
        return m_id;
    }

    void FakePowerSupply::SetId(std::string id)
    {
        m_id = std::move(id);
    }

    OperationResult FakePowerSupply::Initialize(const nlohmann::json& configuration)
    {
        try
        {
            const auto resource = configuration.value("hw-rsrc", std::string{});
            if (resource.empty())
            {
                return OperationResult::Failure(
                    "POWER_SUPPLY_RESOURCE_MISSING",
                    "The hw-rsrc parameter is required.");
            }
            if (configuration.value("failInitialization", false))
            {
                return OperationResult::Failure(
                    "POWER_SUPPLY_INITIALIZATION_FORCED_FAILURE",
                    "Fake PowerSupply initialization was configured to fail.");
            }
            m_initialized = true;
            Publish("[PowerSupply:" + m_id + "] Initialize " + resource);
            return OperationResult::Success();
        }
        catch (const std::exception& exception)
        {
            return OperationResult::Failure("POWER_SUPPLY_CONFIGURATION_INVALID", exception.what());
        }
    }

    void FakePowerSupply::Shutdown() noexcept
    {
        if (m_initialized)
        {
            Publish("[PowerSupply:" + m_id + "] Shutdown");
        }
        m_initialized = false;
        m_channelState.clear();
    }

    OperationResult FakePowerSupply::TurnOn(int channel)
    {
        if (const auto result = EnsureReady(channel); !result.Succeeded())
        {
            return result;
        }
        m_channelState[channel] = true;
        Publish("[PowerSupply:" + m_id + "] TurnOn channel " + std::to_string(channel));
        return OperationResult::Success();
    }

    OperationResult FakePowerSupply::TurnOff(int channel)
    {
        if (const auto result = EnsureReady(channel); !result.Succeeded())
        {
            return result;
        }
        m_channelState[channel] = false;
        Publish("[PowerSupply:" + m_id + "] TurnOff channel " + std::to_string(channel));
        return OperationResult::Success();
    }

    OperationResult FakePowerSupply::SetVoltage(int channel, double voltage)
    {
        if (const auto result = EnsureReady(channel); !result.Succeeded())
        {
            return result;
        }
        if (voltage < 0.0)
        {
            return OperationResult::Failure("POWER_SUPPLY_VOLTAGE_INVALID", "Voltage cannot be negative.");
        }
        m_voltages[channel] = voltage;
        std::ostringstream text;
        text << "[PowerSupply:" << m_id << "] SetVoltage channel " << channel << " = " << voltage;
        Publish(text.str());
        return OperationResult::Success();
    }

    OperationResult FakePowerSupply::SetCurrent(int channel, double current)
    {
        if (const auto result = EnsureReady(channel); !result.Succeeded())
        {
            return result;
        }
        if (current < 0.0)
        {
            return OperationResult::Failure("POWER_SUPPLY_CURRENT_INVALID", "Current cannot be negative.");
        }
        m_currentLimits[channel] = current;
        std::ostringstream text;
        text << "[PowerSupply:" << m_id << "] SetCurrent channel " << channel << " = " << current;
        Publish(text.str());
        return OperationResult::Success();
    }

    bool FakePowerSupply::IsInitialized() const noexcept
    {
        return m_initialized;
    }

    bool FakePowerSupply::IsChannelOn(int channel) const
    {
        const auto found = m_channelState.find(channel);
        return found != m_channelState.end() && found->second;
    }

    double FakePowerSupply::Voltage(int channel) const
    {
        const auto found = m_voltages.find(channel);
        return found == m_voltages.end() ? 0.0 : found->second;
    }

    double FakePowerSupply::CurrentLimit(int channel) const
    {
        const auto found = m_currentLimits.find(channel);
        return found == m_currentLimits.end() ? 0.0 : found->second;
    }

    OperationResult FakePowerSupply::EnsureReady(int channel) const
    {
        if (!m_initialized)
        {
            return OperationResult::Failure(
                "POWER_SUPPLY_NOT_INITIALIZED",
                "The PowerSupply instrument is not initialized.");
        }
        if (channel < 0)
        {
            return OperationResult::Failure("POWER_SUPPLY_CHANNEL_INVALID", "The channel must be zero or greater.");
        }
        return OperationResult::Success();
    }

    void FakePowerSupply::Publish(std::string message) noexcept
    {
        m_eventSink.Publish({
            EngineEventKind::InstrumentOperation,
            EngineEventSeverity::Information,
            m_id,
            std::move(message)});
    }
}

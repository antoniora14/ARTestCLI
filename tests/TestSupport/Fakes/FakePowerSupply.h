#pragma once

#include "IPowerSupply.h"
#include "ARTestEngine.Core/Execution/IEventSink.h"

#include <unordered_map>

namespace artest
{
    inline constexpr auto FakePowerSupplyType = "PowerSupply";

    class FakePowerSupply final : public IPowerSupply
    {
    public:
        explicit FakePowerSupply(IEventSink& eventSink) noexcept;

        [[nodiscard]] std::string GetId() const override;
        void SetId(std::string id) override;
        [[nodiscard]] OperationResult Initialize(const nlohmann::json& configuration) override;
        [[nodiscard]] OperationResult Shutdown() override;
        [[nodiscard]] OperationResult TurnOn(int channel) override;
        [[nodiscard]] OperationResult TurnOff(int channel) override;
        [[nodiscard]] OperationResult SetVoltage(int channel, double voltage) override;
        [[nodiscard]] OperationResult SetCurrent(int channel, double current) override;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool IsChannelOn(int channel) const;
        [[nodiscard]] double Voltage(int channel) const;
        [[nodiscard]] double CurrentLimit(int channel) const;

    private:
        [[nodiscard]] OperationResult EnsureReady(int channel) const;
        void Publish(std::string message) noexcept;

        IEventSink& m_eventSink;
        std::string m_id;
        bool m_initialized = false;
        int m_remainingTurnOnFailures = 0;
        bool m_failShutdown = false;
        std::unordered_map<int, bool> m_channelState;
        std::unordered_map<int, double> m_voltages;
        std::unordered_map<int, double> m_currentLimits;
    };
}

#pragma once

#include "../ICanDevice.h"
#include "../../Execution/IEventSink.h"

#include <vector>

namespace artest
{
    inline constexpr auto FakeCanDeviceType = "CAN";

    struct SentCanMessage
    {
        int channel = -1;
        std::uint32_t messageId = 0;
        std::vector<std::uint8_t> data;
    };

    class FakeCanDevice final : public ICanDevice
    {
    public:
        explicit FakeCanDevice(IEventSink& eventSink) noexcept;

        [[nodiscard]] std::string GetId() const override;
        void SetId(std::string id) override;
        [[nodiscard]] OperationResult Initialize(const nlohmann::json& configuration) override;
        void Shutdown() noexcept override;
        [[nodiscard]] OperationResult SendMessage(
            int channel,
            std::uint32_t messageId,
            const std::vector<std::uint8_t>& data) override;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const std::vector<SentCanMessage>& SentMessages() const noexcept;

    private:
        void Publish(std::string message) noexcept;

        IEventSink& m_eventSink;
        std::string m_id;
        bool m_initialized = false;
        std::vector<SentCanMessage> m_messages;
    };
}

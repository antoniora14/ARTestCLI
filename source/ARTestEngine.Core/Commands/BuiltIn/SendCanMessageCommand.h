#pragma once

#include "../ICommand.h"
#include "../../Instruments/ICanDevice.h"

#include <cstdint>
#include <vector>

namespace artest
{
    inline constexpr auto SendCanMessageCommandName = "CAN.SendMessage";

    class SendCanMessageCommand final : public ICommand
    {
    public:
        [[nodiscard]] std::string Name() const override;
        [[nodiscard]] OperationResult Configure(
            const nlohmann::json& parameters,
            std::shared_ptr<IInstrument> instrument) override;
        [[nodiscard]] OperationResult Validate() const override;
        [[nodiscard]] StepResult Execute(
            ExecutionContext& context,
            const CancellationToken& cancellation) override;

    private:
        std::shared_ptr<ICanDevice> m_canDevice;
        int m_channel = -1;
        std::uint32_t m_messageId = 0;
        int m_dlc = -1;
        std::vector<std::uint8_t> m_data;
        std::string m_configurationError;
    };
}

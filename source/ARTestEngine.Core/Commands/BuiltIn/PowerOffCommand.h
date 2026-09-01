#pragma once

#include "../ICommand.h"
#include "../../Instruments/IPowerSupply.h"

namespace artest
{
    inline constexpr auto PowerOffCommandName = "PowerSupply.TurnOff";

    class PowerOffCommand final : public ICommand
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
        std::shared_ptr<IPowerSupply> m_powerSupply;
        int m_channel = -1;
    };
}

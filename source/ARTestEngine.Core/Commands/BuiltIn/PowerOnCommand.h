#pragma once

#include "../ICommand.h"
#include "../../Instruments/IPowerSupply.h"

namespace artest
{
    inline constexpr auto PowerOnCommandName = "PowerSupply.TurnOn";

    class PowerOnCommand final : public ICommand
    {
    public:
        [[nodiscard]] std::string Name() const override;
        [[nodiscard]] OperationResult Configure(
            const nlohmann::json& parameters,
            std::shared_ptr<IInstrument> instrument) override;
        [[nodiscard]] OperationResult Validate() const override;
        [[nodiscard]] StepResult Execute(ExecutionContext& context) override;

    private:
        std::shared_ptr<IPowerSupply> m_powerSupply;
        int m_channel = -1;
        double m_voltage = -1.0;
        double m_currentLimit = -1.0;
    };
}

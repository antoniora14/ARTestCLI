#pragma once

#include "../ICommand.h"

namespace artest
{
    inline constexpr auto WaitCommandName = "Time.WaitMs";

    class WaitCommand final : public ICommand
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
        int m_milliseconds = -1;
    };
}

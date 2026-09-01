#include "PowerOffCommand.h"

namespace artest
{
    std::string PowerOffCommand::Name() const
    {
        return PowerOffCommandName;
    }

    OperationResult PowerOffCommand::Configure(
        const nlohmann::json& parameters,
        std::shared_ptr<IInstrument> instrument)
    {
        m_powerSupply = std::dynamic_pointer_cast<IPowerSupply>(std::move(instrument));
        try
        {
            m_channel = parameters.value("channel", -1);
            return OperationResult::Success();
        }
        catch (const std::exception& exception)
        {
            return OperationResult::Failure("POWER_OFF_CONFIGURATION_INVALID", exception.what());
        }
    }

    OperationResult PowerOffCommand::Validate() const
    {
        if (!m_powerSupply)
        {
            return OperationResult::Failure(
                "POWER_SUPPLY_REQUIRED",
                "The command requires a PowerSupply instrument.");
        }
        if (m_channel < 0)
        {
            return OperationResult::Failure("POWER_SUPPLY_CHANNEL_INVALID", "The channel must be zero or greater.");
        }
        return OperationResult::Success();
    }

    StepResult PowerOffCommand::Execute(
        ExecutionContext&,
        const CancellationToken& cancellation)
    {
        if (cancellation.Reason() != CancellationReason::None)
        {
            return cancellation.IsTimedOut() ? StepResult::Timeout() : StepResult::Cancel();
        }
        if (!m_powerSupply)
        {
            return StepResult::Error("The bound instrument is not a PowerSupply.");
        }
        const auto result = m_powerSupply->TurnOff(m_channel);
        if (!result.Succeeded())
        {
            return StepResult::Error(
                result.diagnostics.empty() ? "The instrument operation failed." : result.diagnostics.front().message);
        }
        return StepResult::Pass();
    }
}

#include "PowerOnCommand.h"

namespace
{
    artest::StepResult ToStepResult(const artest::OperationResult& result)
    {
        if (result.Succeeded())
        {
            return artest::StepResult::Pass();
        }
        return artest::StepResult::Error(
            result.diagnostics.empty() ? "The instrument operation failed." : result.diagnostics.front().message);
    }
}

namespace artest
{
    std::string PowerOnCommand::Name() const
    {
        return PowerOnCommandName;
    }

    OperationResult PowerOnCommand::Configure(
        const nlohmann::json& parameters,
        std::shared_ptr<IInstrument> instrument)
    {
        m_powerSupply = std::dynamic_pointer_cast<IPowerSupply>(std::move(instrument));
        try
        {
            m_channel = parameters.value("channel", -1);
            m_voltage = parameters.value("voltage", -1.0);
            m_currentLimit = parameters.value("currentLimit", -1.0);
            return OperationResult::Success();
        }
        catch (const std::exception& exception)
        {
            return OperationResult::Failure("POWER_ON_CONFIGURATION_INVALID", exception.what());
        }
    }

    OperationResult PowerOnCommand::Validate() const
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
        if (m_voltage < 0.0)
        {
            return OperationResult::Failure("POWER_SUPPLY_VOLTAGE_INVALID", "The voltage must be zero or greater.");
        }
        if (m_currentLimit < 0.0)
        {
            return OperationResult::Failure(
                "POWER_SUPPLY_CURRENT_INVALID",
                "The current limit must be zero or greater.");
        }
        return OperationResult::Success();
    }

    StepResult PowerOnCommand::Execute(
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

        auto result = m_powerSupply->SetVoltage(m_channel, m_voltage);
        if (!result.Succeeded())
        {
            return ToStepResult(result);
        }
        if (cancellation.Reason() != CancellationReason::None)
        {
            return cancellation.IsTimedOut() ? StepResult::Timeout() : StepResult::Cancel();
        }
        result = m_powerSupply->SetCurrent(m_channel, m_currentLimit);
        if (!result.Succeeded())
        {
            return ToStepResult(result);
        }
        if (cancellation.Reason() != CancellationReason::None)
        {
            return cancellation.IsTimedOut() ? StepResult::Timeout() : StepResult::Cancel();
        }
        return ToStepResult(m_powerSupply->TurnOn(m_channel));
    }
}

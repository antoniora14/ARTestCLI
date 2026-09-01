#include "WaitCommand.h"

#include <chrono>

namespace artest
{
    std::string WaitCommand::Name() const
    {
        return WaitCommandName;
    }

    OperationResult WaitCommand::Configure(
        const nlohmann::json& parameters,
        std::shared_ptr<IInstrument>)
    {
        try
        {
            m_milliseconds = parameters.value("milliseconds", -1);
            return OperationResult::Success();
        }
        catch (const std::exception& exception)
        {
            return OperationResult::Failure("WAIT_CONFIGURATION_INVALID", exception.what());
        }
    }

    OperationResult WaitCommand::Validate() const
    {
        if (m_milliseconds < 0)
        {
            return OperationResult::Failure(
                "WAIT_DURATION_INVALID",
                "The milliseconds parameter must be zero or greater.");
        }
        return OperationResult::Success();
    }

    StepResult WaitCommand::Execute(
        ExecutionContext&,
        const CancellationToken& cancellation)
    {
        if (cancellation.WaitFor(std::chrono::milliseconds(m_milliseconds)))
        {
            return cancellation.Reason() == CancellationReason::TimedOut
                ? StepResult::Timeout()
                : StepResult::Cancel();
        }
        return StepResult::Pass();
    }
}

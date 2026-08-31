#include "WaitCommand.h"

#include <chrono>
#include <thread>

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

    StepResult WaitCommand::Execute(ExecutionContext&)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_milliseconds));
        return StepResult::Pass();
    }
}

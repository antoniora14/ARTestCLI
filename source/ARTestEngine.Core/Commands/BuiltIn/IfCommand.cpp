#include "IfCommand.h"

namespace artest
{
    std::string IfCommand::Name() const
    {
        return IfCommandName;
    }

    OperationResult IfCommand::Configure(
        const nlohmann::json& parameters,
        std::shared_ptr<IInstrument>)
    {
        try
        {
            m_condition = parameters.value("condition", std::string{});
            return OperationResult::Success();
        }
        catch (const std::exception& exception)
        {
            return OperationResult::Failure("IF_CONFIGURATION_INVALID", exception.what());
        }
    }

    OperationResult IfCommand::Validate() const
    {
        return OperationResult::Failure(
            "IF_NOT_IMPLEMENTED",
            "IF is reserved but is not implemented in script version 1.");
    }

    StepResult IfCommand::Execute(ExecutionContext&, const CancellationToken&)
    {
        return StepResult::Error("IF execution is not implemented.");
    }
}

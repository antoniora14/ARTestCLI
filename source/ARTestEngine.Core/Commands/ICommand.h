#pragma once

#include "../Diagnostics.h"
#include "../Execution/ExecutionContext.h"
#include "../Execution/ExecutionResult.h"
#include "../Execution/Cancellation.h"
#include "../Instruments/IInstrument.h"
#include "../../ThirdParty/json.hpp"

#include <memory>
#include <string>

namespace artest
{
    class ICommand
    {
    public:
        virtual ~ICommand() = default;

        [[nodiscard]] virtual std::string Name() const = 0;
        [[nodiscard]] virtual OperationResult Configure(
            const nlohmann::json& parameters,
            std::shared_ptr<IInstrument> instrument) = 0;
        [[nodiscard]] virtual OperationResult Validate() const = 0;
        [[nodiscard]] virtual StepResult Execute(
            ExecutionContext& context,
            const CancellationToken& cancellation) = 0;
    };
}

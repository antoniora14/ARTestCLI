#pragma once
#include "../Model/ExecutionPolicy.h"
#include "../Commands/ICommand.h"
#include <cstdint>
#include <memory>
#include <string>

namespace artest
{
    // Session-owned executable state, deliberately separate from CompiledStep.
    struct RuntimeStep
    {
        std::uint64_t stepId = 0;
        std::string commandName;
        std::unique_ptr<ICommand> command;
        StepExecutionPolicy policy;
    };
}

#pragma once

#include "../Commands/ICommand.h"

#include <cstdint>
#include <memory>
#include <string>

namespace artest
{
    struct CompiledStep
    {
        std::uint64_t stepId = 0;
        std::string commandName;
        std::unique_ptr<ICommand> command;
    };
}

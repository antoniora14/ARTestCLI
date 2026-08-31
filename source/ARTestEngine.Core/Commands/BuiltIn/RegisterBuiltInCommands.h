#pragma once

#include "../CommandRegistry.h"

namespace artest
{
    [[nodiscard]] OperationResult RegisterBuiltInCommands(CommandRegistry& registry);
}

#pragma once

#include "ExecutionPolicy.h"
#include "../../ThirdParty/json.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace artest
{
    // Reusable, copyable compilation output. No executable instance or DLL lifetime.
    struct CompiledStep
    {
        std::uint64_t stepId = 0;
        std::string commandName;
        std::string componentType;
        nlohmann::json parameters = nlohmann::json::object();
        std::optional<std::string> instrumentId;
        StepExecutionPolicy policy;
    };
}

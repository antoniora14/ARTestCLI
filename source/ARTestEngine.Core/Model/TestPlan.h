#pragma once

#include "ExecutionPolicy.h"
#include "../../ThirdParty/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace artest
{
    struct InstrumentDefinition
    {
        std::string type;
        std::string id;
        nlohmann::json configuration = nlohmann::json::object();
    };

    struct StepDefinition
    {
        std::uint64_t stepId = 0;
        std::string commandName;
        std::optional<std::string> instrumentId;
        nlohmann::json parameters = nlohmann::json::object();
        StepExecutionPolicy policy;
    };

    struct TestPlan
    {
        static constexpr int CurrentVersion = 1;
        static constexpr const char* FormatName = "ARTest.Script";

        int version = CurrentVersion;
        std::vector<InstrumentDefinition> instruments;
        std::vector<StepDefinition> steps;
    };
}

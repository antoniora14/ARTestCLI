#pragma once

#include "../Diagnostics.h"
#include "../Model/TestPlan.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace artest
{
    class JsonTestPlanParser final
    {
    public:
        [[nodiscard]] ValueResult<TestPlan> ParseFile(const std::filesystem::path& path) const;
        [[nodiscard]] ValueResult<TestPlan> ParseText(std::string_view jsonText, std::string source = "<memory>") const;
    };
}

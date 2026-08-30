#pragma once

#include "Diagnostics.h"
#include "../ThirdParty/json.hpp"

#include <filesystem>

struct ScriptDocument
{
    static constexpr int CurrentVersion = 1;
    static constexpr const char* FormatName = "ARTest.Script";

    nlohmann::json instruments;
    nlohmann::json commands;
};

class ScriptDocumentLoader final
{
public:
    [[nodiscard]] static ValueResult<ScriptDocument> Load(const std::filesystem::path& path);
};

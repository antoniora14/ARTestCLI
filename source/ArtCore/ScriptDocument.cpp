#include "ScriptDocument.h"

#include <fstream>
#include <limits>

namespace
{
    constexpr std::uintmax_t MaximumScriptSize = 4U * 1024U * 1024U;

    [[nodiscard]] ValueResult<ScriptDocument> Failure(
        std::string code,
        std::string message,
        const std::filesystem::path& path)
    {
        ValueResult<ScriptDocument> result;
        result.diagnostics.push_back({
            DiagnosticSeverity::Error,
            std::move(code),
            std::move(message),
            path.string()});
        return result;
    }
}

ValueResult<ScriptDocument> ScriptDocumentLoader::Load(const std::filesystem::path& path)
{
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (sizeError)
    {
        return Failure("SCRIPT_FILE_UNAVAILABLE", "The script file could not be inspected.", path);
    }
    if (size > MaximumScriptSize)
    {
        return Failure("SCRIPT_FILE_TOO_LARGE", "The script exceeds the 4 MB safety limit.", path);
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return Failure("SCRIPT_OPEN_FAILED", "The script file could not be opened.", path);
    }

    nlohmann::json root;
    try
    {
        input >> root;
    }
    catch (const std::exception& exception)
    {
        return Failure("SCRIPT_JSON_INVALID", std::string{"Invalid JSON: "} + exception.what(), path);
    }

    if (!root.is_object())
    {
        return Failure("SCRIPT_ROOT_INVALID", "The script root must be a JSON object.", path);
    }
    if (root.value("format", std::string{}) != ScriptDocument::FormatName)
    {
        return Failure("SCRIPT_FORMAT_INVALID", "The script format must be ARTest.Script.", path);
    }
    if (!root.contains("version") || !root["version"].is_number_integer())
    {
        return Failure("SCRIPT_VERSION_MISSING", "The script must declare an integer version.", path);
    }
    if (root["version"].get<int>() != ScriptDocument::CurrentVersion)
    {
        return Failure("SCRIPT_VERSION_UNSUPPORTED", "The script version is not supported.", path);
    }
    if (!root.contains("instruments") || !root["instruments"].is_array())
    {
        return Failure("SCRIPT_INSTRUMENTS_INVALID", "The instruments member must be an array.", path);
    }
    if (!root.contains("commands") || !root["commands"].is_array())
    {
        return Failure("SCRIPT_COMMANDS_INVALID", "The commands member must be an array.", path);
    }

    ValueResult<ScriptDocument> result;
    result.value = ScriptDocument{root["instruments"], root["commands"]};
    return result;
}

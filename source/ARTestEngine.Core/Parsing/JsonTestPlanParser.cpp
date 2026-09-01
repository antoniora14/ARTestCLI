#include "JsonTestPlanParser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace
{
    constexpr std::uintmax_t MaximumScriptSize = 4U * 1024U * 1024U;

    [[nodiscard]] std::string At(const std::string& source, const std::string& member)
    {
        return member.empty() ? source : source + ":" + member;
    }
}

namespace artest
{
    ValueResult<TestPlan> JsonTestPlanParser::ParseFile(const std::filesystem::path& path) const
    {
        ValueResult<TestPlan> result;
        std::error_code sizeError;
        const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
        if (sizeError)
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_FILE_UNAVAILABLE",
                "The script file could not be inspected.",
                path.string()});
            return result;
        }
        if (size > MaximumScriptSize)
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_FILE_TOO_LARGE",
                "The script exceeds the 4 MB safety limit.",
                path.string()});
            return result;
        }

        std::ifstream input{path, std::ios::binary};
        if (!input)
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_OPEN_FAILED",
                "The script file could not be opened.",
                path.string()});
            return result;
        }

        std::ostringstream content;
        content << input.rdbuf();
        return ParseText(content.str(), path.string());
    }

    ValueResult<TestPlan> JsonTestPlanParser::ParseText(
        std::string_view jsonText,
        std::string source) const
    {
        ValueResult<TestPlan> result;
        nlohmann::json root;
        try
        {
            root = nlohmann::json::parse(jsonText);
        }
        catch (const std::exception& exception)
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_JSON_INVALID",
                std::string{"Invalid JSON: "} + exception.what(),
                std::move(source)});
            return result;
        }

        if (!root.is_object())
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_ROOT_INVALID",
                "The script root must be a JSON object.",
                source});
            return result;
        }
        if (!root.contains("format")
            || !root["format"].is_string()
            || root["format"].get<std::string>() != TestPlan::FormatName)
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_FORMAT_INVALID",
                "The script format must be ARTest.Script.",
                At(source, "format")});
        }
        if (!root.contains("version") || !root["version"].is_number_integer())
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_VERSION_MISSING",
                "The script must declare an integer version.",
                At(source, "version")});
        }
        else
        {
            try
            {
                if (root["version"].get<int>() != TestPlan::CurrentVersion)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error,
                        "SCRIPT_VERSION_UNSUPPORTED",
                        "The script version is not supported.",
                        At(source, "version")});
                }
            }
            catch (const std::exception&)
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "SCRIPT_VERSION_UNSUPPORTED",
                    "The script version is outside the supported range.",
                    At(source, "version")});
            }
        }
        if (!root.contains("instruments") || !root["instruments"].is_array())
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_INSTRUMENTS_INVALID",
                "The instruments member must be an array.",
                At(source, "instruments")});
        }
        if (!root.contains("commands") || !root["commands"].is_array())
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_COMMANDS_INVALID",
                "The commands member must be an array.",
                At(source, "commands")});
        }
        if (ContainsErrors(result.diagnostics))
        {
            return result;
        }

        TestPlan plan;
        plan.version = root["version"].get<int>();

        for (std::size_t index = 0; index < root["instruments"].size(); ++index)
        {
            const auto& definition = root["instruments"][index];
            const std::string location = At(source, "instruments[" + std::to_string(index) + "]");
            if (!definition.is_object()
                || !definition.contains("type") || !definition["type"].is_string()
                || !definition.contains("id") || !definition["id"].is_string()
                || !definition.contains("config") || !definition["config"].is_object())
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "INSTRUMENT_SCHEMA_INVALID",
                    "The instrument definition is incomplete or has invalid types.",
                    location});
                continue;
            }

            plan.instruments.push_back({
                definition["type"].get<std::string>(),
                definition["id"].get<std::string>(),
                definition["config"]});
        }

        for (std::size_t index = 0; index < root["commands"].size(); ++index)
        {
            const auto& definition = root["commands"][index];
            const std::string location = At(source, "commands[" + std::to_string(index) + "]");
            if (!definition.is_object()
                || !definition.contains("stepId") || !definition["stepId"].is_number_integer()
                || !definition.contains("name") || !definition["name"].is_string()
                || !definition.contains("instrument") || !definition["instrument"].is_string()
                || !definition.contains("params") || !definition["params"].is_object())
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "COMMAND_SCHEMA_INVALID",
                    "The command definition is incomplete or has invalid types.",
                    location});
                continue;
            }

            std::uint64_t stepId = 0;
            try
            {
                if (definition["stepId"].is_number_unsigned())
                {
                    stepId = definition["stepId"].get<std::uint64_t>();
                }
                else
                {
                    const auto signedId = definition["stepId"].get<std::int64_t>();
                    if (signedId > 0)
                    {
                        stepId = static_cast<std::uint64_t>(signedId);
                    }
                }
            }
            catch (const std::exception&)
            {
                stepId = 0;
            }

            const std::string instrument = definition["instrument"].get<std::string>();
            StepExecutionPolicy policy;
            if (definition.contains("policy"))
            {
                const auto& policyJson = definition["policy"];
                if (!policyJson.is_object())
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error,
                        "COMMAND_POLICY_SCHEMA_INVALID",
                        "The policy member must be an object.",
                        location + ":policy"});
                    continue;
                }

                try
                {
                    if (policyJson.contains("maxAttempts"))
                    {
                        if (!policyJson["maxAttempts"].is_number_integer())
                        {
                            throw std::invalid_argument("maxAttempts must be an integer.");
                        }
                        policy.maxAttempts = policyJson["maxAttempts"].get<int>();
                    }
                    if (policyJson.contains("retryDelayMs"))
                    {
                        if (!policyJson["retryDelayMs"].is_number_integer())
                        {
                            throw std::invalid_argument("retryDelayMs must be an integer.");
                        }
                        policy.retryDelay = std::chrono::milliseconds{
                            policyJson["retryDelayMs"].get<std::int64_t>()};
                    }
                    if (policyJson.contains("timeoutMs"))
                    {
                        if (!policyJson["timeoutMs"].is_number_integer())
                        {
                            throw std::invalid_argument("timeoutMs must be an integer.");
                        }
                        policy.timeout = std::chrono::milliseconds{
                            policyJson["timeoutMs"].get<std::int64_t>()};
                    }
                    if (policyJson.contains("onFailure"))
                    {
                        if (!policyJson["onFailure"].is_string())
                        {
                            throw std::invalid_argument("onFailure must be a string.");
                        }
                        const auto action = policyJson["onFailure"].get<std::string>();
                        if (action == "stop")
                        {
                            policy.onFailure = FailureAction::Stop;
                        }
                        else if (action == "continue")
                        {
                            policy.onFailure = FailureAction::Continue;
                        }
                        else
                        {
                            throw std::invalid_argument("onFailure must be 'stop' or 'continue'.");
                        }
                    }
                }
                catch (const std::exception& exception)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error,
                        "COMMAND_POLICY_SCHEMA_INVALID",
                        exception.what(),
                        location + ":policy"});
                    continue;
                }
            }

            plan.steps.push_back({
                stepId,
                definition["name"].get<std::string>(),
                instrument == "NoInstrument" || instrument.empty()
                    ? std::nullopt
                    : std::optional<std::string>{instrument},
                definition["params"],
                policy});
        }

        if (!ContainsErrors(result.diagnostics))
        {
            result.value = std::move(plan);
        }
        return result;
    }
}

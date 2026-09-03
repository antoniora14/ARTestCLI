#include "EngineMarshalling.h"
#include <algorithm>
#include <stdexcept>
namespace artest::engine
{
[[nodiscard]] std::string ToString(ARTestStringView value)
{
    return value.data == nullptr ? std::string{} : std::string{value.data, value.size};
}

[[nodiscard]] std::string PayloadText(const ARTestPayloadView *payload)
{
    if (payload == nullptr || payload->struct_size < sizeof(ARTestPayloadView) ||
        payload->encoding != ARTEST_PAYLOAD_ENCODING_JSON_UTF8 ||
        (payload->bytes.data == nullptr && payload->bytes.size != 0U))
    {
        throw std::invalid_argument("A valid JSON UTF-8 payload is required.");
    }
    return {reinterpret_cast<const char *>(payload->bytes.data), payload->bytes.size};
}

[[nodiscard]] ARTestStringView View(const std::string &value) noexcept
{
    return {value.data(), value.size()};
}

[[nodiscard]] ARTestPayloadView JsonPayload(const std::string &value) noexcept
{
    static const std::string schema = "artest.schema.generic-json.v1";
    static const std::string media = "application/json; charset=utf-8";
    return {sizeof(ARTestPayloadView),
            ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema),
            View(media),
            {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()}};
}

void SetError(ARTestErrorBuffer *error, const std::string &message) noexcept
{
    if (error == nullptr)
        return;
    error->required_size = message.size() + 1U;
    if (error->data == nullptr || error->capacity == 0U)
        return;
    const auto count = (std::min)(message.size(), error->capacity - 1U);
    std::copy_n(message.data(), count, error->data);
    error->data[count] = '\0';
}

[[nodiscard]] std::string DiagnosticsText(const std::vector<artest::Diagnostic> &diagnostics)
{
    if (diagnostics.empty())
        return "The operation failed.";
    std::string text = diagnostics.front().code + ": " + diagnostics.front().message;
    if (!diagnostics.front().location.empty())
        text += " (" + diagnostics.front().location + ")";
    return text;
}

[[nodiscard]] const char *StepStatusText(artest::StepStatus value) noexcept
{
    switch (value)
    {
    case artest::StepStatus::Passed:
        return "passed";
    case artest::StepStatus::Failed:
        return "failed";
    case artest::StepStatus::Error:
        return "error";
    case artest::StepStatus::Skipped:
        return "skipped";
    case artest::StepStatus::Cancelled:
        return "cancelled";
    case artest::StepStatus::TimedOut:
        return "timedOut";
    }
    return "error";
}

[[nodiscard]] const char *RunStatusText(artest::RunStatus value) noexcept
{
    switch (value)
    {
    case artest::RunStatus::Passed:
        return "passed";
    case artest::RunStatus::Failed:
        return "failed";
    case artest::RunStatus::Error:
        return "error";
    case artest::RunStatus::Cancelled:
        return "cancelled";
    case artest::RunStatus::TimedOut:
        return "timedOut";
    }
    return "error";
}

[[nodiscard]] nlohmann::json SerializeDiagnostic(const artest::Diagnostic &value)
{
    return {{"severity", value.severity == artest::DiagnosticSeverity::Error     ? "error"
                         : value.severity == artest::DiagnosticSeverity::Warning ? "warning"
                                                                                 : "information"},
            {"code", value.code},
            {"message", value.message},
            {"location", value.location}};
}

[[nodiscard]] nlohmann::json SerializeCompileResult(
    bool valid, std::size_t instrumentCount, std::size_t stepCount,
    const std::vector<artest::Diagnostic> &diagnostics)
{
    nlohmann::json result{
        {"schema", "artest.schema.compile-result.v1"},
        {"valid", valid},
        {"summary", {{"instrumentDefinitions", instrumentCount}, {"steps", stepCount}}},
        {"diagnostics", nlohmann::json::array()}};
    for (const auto &diagnostic : diagnostics)
        result["diagnostics"].push_back(SerializeDiagnostic(diagnostic));
    return result;
}

[[nodiscard]] nlohmann::json SerializeResult(const artest::RunResult &value)
{
    nlohmann::json result{{"schema", "artest.schema.run-result.v1"},
                          {"status", RunStatusText(value.status)},
                          {"failureKind", static_cast<int>(value.failureKind)},
                          {"summary",
                           {{"plannedSteps", value.summary.plannedSteps},
                            {"executedSteps", value.summary.executedSteps},
                            {"passedSteps", value.summary.passedSteps},
                            {"failedSteps", value.summary.failedSteps},
                            {"errorSteps", value.summary.errorSteps},
                            {"cancelledSteps", value.summary.cancelledSteps},
                            {"timedOutSteps", value.summary.timedOutSteps},
                            {"skippedSteps", value.summary.skippedSteps},
                            {"totalAttempts", value.summary.totalAttempts},
                            {"durationMs", value.summary.duration.count()}}},
                          {"diagnostics", nlohmann::json::array()},
                          {"steps", nlohmann::json::array()}};
    for (const auto &diagnostic : value.diagnostics)
        result["diagnostics"].push_back(SerializeDiagnostic(diagnostic));
    for (const auto &step : value.steps)
    {
        nlohmann::json item{{"stepId", step.stepId},
                            {"command", step.commandName},
                            {"status", StepStatusText(step.result.status)},
                            {"message", step.result.message},
                            {"durationMs", step.duration.count()},
                            {"attempts", nlohmann::json::array()}};
        for (const auto &attempt : step.attempts)
            item["attempts"].push_back({{"attempt", attempt.attempt},
                                        {"status", StepStatusText(attempt.result.status)},
                                        {"message", attempt.result.message},
                                        {"durationMs", attempt.duration.count()}});
        result["steps"].push_back(std::move(item));
    }
    return result;
}

[[nodiscard]] ARTestStatus WriteJson(const nlohmann::json &value, const ARTestResultSinkV0 *sink,
                                     ARTestErrorBuffer *error)
{
    if (sink == nullptr || sink->struct_size < sizeof(ARTestResultSinkV0) || sink->write == nullptr)
    {
        SetError(error, "A valid result sink is required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    const auto text = value.dump();
    const auto payload = JsonPayload(text);
    return sink->write(sink->sink_context, &payload, error);
}

} // namespace artest::engine

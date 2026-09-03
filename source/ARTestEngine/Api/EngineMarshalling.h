#pragma once
#include "../../ARTest.SDK/include/ARTestEngineApi.h"
#include "../../ARTestEngine.Core/Execution/ExecutionResult.h"
#include "../../ThirdParty/json.hpp"
#include <string>
namespace artest::engine
{
[[nodiscard]] std::string ToString(ARTestStringView value);
[[nodiscard]] std::string PayloadText(const ARTestPayloadView *payload);
[[nodiscard]] ARTestStringView View(const std::string &value) noexcept;
[[nodiscard]] ARTestPayloadView JsonPayload(const std::string &value) noexcept;
void SetError(ARTestErrorBuffer *error, const std::string &message) noexcept;
[[nodiscard]] std::string DiagnosticsText(const std::vector<artest::Diagnostic> &diagnostics);
[[nodiscard]] const char *StepStatusText(artest::StepStatus value) noexcept;
[[nodiscard]] const char *RunStatusText(artest::RunStatus value) noexcept;
[[nodiscard]] nlohmann::json SerializeDiagnostic(const artest::Diagnostic &value);
[[nodiscard]] nlohmann::json SerializeCompileResult(
    bool valid, std::size_t instrumentCount, std::size_t stepCount,
    const std::vector<artest::Diagnostic> &diagnostics);
[[nodiscard]] nlohmann::json SerializeResult(const artest::RunResult &value);
[[nodiscard]] ARTestStatus WriteJson(const nlohmann::json &value, const ARTestResultSinkV0 *sink,
                                     ARTestErrorBuffer *error);
} // namespace artest::engine

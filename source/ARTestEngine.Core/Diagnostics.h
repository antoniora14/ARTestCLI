#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace artest
{
    enum class DiagnosticSeverity
    {
        Information,
        Warning,
        Error
    };

    struct Diagnostic
    {
        DiagnosticSeverity severity = DiagnosticSeverity::Error;
        std::string code;
        std::string message;
        std::string location;
    };

    [[nodiscard]] inline bool ContainsErrors(const std::vector<Diagnostic>& diagnostics) noexcept
    {
        for (const auto& diagnostic : diagnostics)
        {
            if (diagnostic.severity == DiagnosticSeverity::Error)
            {
                return true;
            }
        }
        return false;
    }

    struct OperationResult
    {
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return !ContainsErrors(diagnostics);
        }

        [[nodiscard]] static OperationResult Success()
        {
            return {};
        }

        [[nodiscard]] static OperationResult Failure(
            std::string code,
            std::string message,
            std::string location = {})
        {
            return {{{DiagnosticSeverity::Error, std::move(code), std::move(message), std::move(location)}}};
        }
    };

    template <typename T>
    struct ValueResult
    {
        std::optional<T> value;
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return value.has_value() && !ContainsErrors(diagnostics);
        }
    };
}

#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace artest::cli
{
    class CliApplication final
    {
    public:
        CliApplication(std::istream& input, std::ostream& output, std::ostream& error) noexcept;
        [[nodiscard]] int Run(const std::vector<std::string>& arguments) noexcept;

    private:
        enum class ExitCode
        {
            Success                         = 0,
            InvalidArguments                = 2,
            InvalidScript                   = 3,
            InstrumentInitializationFailed  = 4,
            ExecutionFailed                 = 5,
            ExtensionCatalogInvalid         = 6,
            UnexpectedFailure               = 10
        };

        [[nodiscard]] int RunExtensionCommand(const std::vector<std::string>& arguments);
        [[nodiscard]] int RunCatalogCommand(const std::vector<std::string>& arguments);
        [[nodiscard]] int RunPlan(const std::string& command, const std::string& scriptPath,
            std::unordered_set<std::size_t> breakpoints, const std::string& extensionRoot = {},
            bool compatibilityJson = false);
        [[nodiscard]] bool ReadScript(const std::string& scriptPath, std::string& content) const;

        void PrintHelp() const;
        void PrintCompileDiagnostics(std::string_view reportJson, std::string_view scriptPath) const;
        void PrintEngineFailure(std::string_view operation, int code, std::string_view message) const;

        std::istream& m_input;
        std::ostream& m_output;
        std::ostream& m_error;
    };
}

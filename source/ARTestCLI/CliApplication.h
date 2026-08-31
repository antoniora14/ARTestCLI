#pragma once

#include "../ARTestEngine.Core/Diagnostics.h"

#include <iosfwd>
#include <string>
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
            Success = 0,
            InvalidArguments = 2,
            InvalidScript = 3,
            InstrumentInitializationFailed = 4,
            ExecutionFailed = 5,
            UnexpectedFailure = 10
        };

        void PrintHelp() const;
        void PrintDiagnostics(const std::vector<Diagnostic>& diagnostics) const;

        std::istream& m_input;
        std::ostream& m_output;
        std::ostream& m_error;
    };
}

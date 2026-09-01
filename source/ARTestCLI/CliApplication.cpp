#include "CliApplication.h"

#include "ConsoleEventSink.h"
#include "ConsoleExecutionControl.h"
#include "ConsoleCancellationHandler.h"
#include "../ARTestEngine.Core/Commands/BuiltIn/RegisterBuiltInCommands.h"
#include "../ARTestEngine.Core/Commands/CommandRegistry.h"
#include "../ARTestEngine.Core/Compilation/TestPlanCompiler.h"
#include "../ARTestEngine.Core/Execution/ExecutionContext.h"
#include "../ARTestEngine.Core/Execution/ExecutionSession.h"
#include "../ARTestEngine.Core/Instruments/Fakes/RegisterFakeInstruments.h"
#include "../ARTestEngine.Core/Instruments/InstrumentManager.h"
#include "../ARTestEngine.Core/Instruments/InstrumentRegistry.h"
#include "../ARTestEngine.Core/Parsing/JsonTestPlanParser.h"

#include <exception>
#include <filesystem>
#include <istream>
#include <ostream>
#include <unordered_set>

namespace artest::cli
{
    CliApplication::CliApplication(
        std::istream& input,
        std::ostream& output,
        std::ostream& error) noexcept
        : m_input(input), m_output(output), m_error(error)
    {
    }

    int CliApplication::Run(const std::vector<std::string>& arguments) noexcept
    {
        try
        {
            if (arguments.empty())
            {
                PrintHelp();
                return static_cast<int>(ExitCode::InvalidArguments);
            }

            const auto& command = arguments[0];
            if (command == "help")
            {
                PrintHelp();
                return static_cast<int>(ExitCode::Success);
            }
            if (command != "compile" && command != "run" && command != "debug" && command != "break")
            {
                m_error << "Unknown command: " << command << '\n';
                PrintHelp();
                return static_cast<int>(ExitCode::InvalidArguments);
            }
            if (arguments.size() < 2)
            {
                m_error << "Error: missing JSON script path.\n";
                return static_cast<int>(ExitCode::InvalidArguments);
            }

            std::unordered_set<std::size_t> breakpoints;
            if (command == "break")
            {
                for (std::size_t index = 2; index < arguments.size(); ++index)
                {
                    std::size_t parsedCharacters = 0;
                    const auto breakpoint = std::stoull(arguments[index], &parsedCharacters);
                    if (parsedCharacters != arguments[index].size())
                    {
                        throw std::invalid_argument("Breakpoint values must be non-negative integers.");
                    }
                    breakpoints.insert(static_cast<std::size_t>(breakpoint));
                }
            }
            else if (arguments.size() > 2)
            {
                throw std::invalid_argument("Unexpected command-line arguments.");
            }

            ConsoleEventSink eventSink(m_output, m_error);
            CommandRegistry commandRegistry;
            InstrumentRegistry instrumentRegistry;

            auto registration = RegisterBuiltInCommands(commandRegistry);
            if (!registration.Succeeded())
            {
                PrintDiagnostics(registration.diagnostics);
                return static_cast<int>(ExitCode::UnexpectedFailure);
            }
            registration = RegisterFakeInstruments(instrumentRegistry);
            if (!registration.Succeeded())
            {
                PrintDiagnostics(registration.diagnostics);
                return static_cast<int>(ExitCode::UnexpectedFailure);
            }

            JsonTestPlanParser parser;
            auto plan = parser.ParseFile(std::filesystem::path{arguments[1]});
            if (!plan.Succeeded())
            {
                PrintDiagnostics(plan.diagnostics);
                return static_cast<int>(ExitCode::InvalidScript);
            }

            InstrumentManager instruments(instrumentRegistry, eventSink);
            auto definitions = instruments.LoadDefinitions(plan.value->instruments);
            if (!definitions.Succeeded())
            {
                PrintDiagnostics(definitions.diagnostics);
                return static_cast<int>(ExitCode::InvalidScript);
            }

            TestPlanCompiler compiler(commandRegistry, instruments);
            auto compiled = compiler.Compile(*plan.value);
            if (!compiled.Succeeded())
            {
                PrintDiagnostics(compiled.diagnostics);
                return static_cast<int>(ExitCode::InvalidScript);
            }

            if (command == "compile")
            {
                m_output << "Valid script. No instruments were initialized.\n";
                return static_cast<int>(ExitCode::Success);
            }

            ConsoleExecutionControl control(
                command == "debug",
                std::move(breakpoints),
                m_input,
                m_output);
            ExecutionSession session(
                std::move(*compiled.value),
                instruments,
                eventSink,
                control);
            const auto start = session.Start();
            if (!start.Succeeded())
            {
                PrintDiagnostics(start.diagnostics);
                return static_cast<int>(ExitCode::UnexpectedFailure);
            }

            ConsoleCancellationHandler cancellationHandler(session);
            const auto run = session.Wait();
            if (run.Succeeded())
            {
                return static_cast<int>(ExitCode::Success);
            }
            if (run.failureKind == RunFailureKind::Initialization)
            {
                return static_cast<int>(ExitCode::InstrumentInitializationFailed);
            }
            return static_cast<int>(ExitCode::ExecutionFailed);
        }
        catch (const std::invalid_argument& exception)
        {
            m_error << "Invalid argument: " << exception.what() << '\n';
            return static_cast<int>(ExitCode::InvalidArguments);
        }
        catch (const std::exception& exception)
        {
            m_error << "Unexpected failure: " << exception.what() << '\n';
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }
        catch (...)
        {
            m_error << "Unexpected non-standard failure.\n";
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }
    }

    void CliApplication::PrintHelp() const
    {
        m_output
            << "Usage:\n"
            << "  ARTestCLI <command> <script.json> [options]\n\n"
            << "Commands:\n"
            << "  compile                 Validate the script without initializing instruments.\n"
            << "  run                     Execute the complete script.\n"
            << "  debug                   Pause before every step.\n"
            << "  break idx1 idx2 ...     Pause at zero-based command indices.\n"
            << "  help                    Display this help.\n\n"
            << "During run, debug, or break, press Ctrl+C to request cooperative cancellation.\n"
            << "The engine always attempts instrument cleanup before returning.\n\n"
            << "Examples:\n"
            << "  ARTestCLI compile source/Scripts/TestScript.json\n"
            << "  ARTestCLI run source/Scripts/TestScript.json\n"
            << "  ARTestCLI debug source/Scripts/TestScript.json\n"
            << "  ARTestCLI break source/Scripts/TestScript.json 1 3\n";
    }

    void CliApplication::PrintDiagnostics(const std::vector<Diagnostic>& diagnostics) const
    {
        for (const auto& diagnostic : diagnostics)
        {
            m_error << '[' << diagnostic.code << ']';
            if (!diagnostic.location.empty())
            {
                m_error << ' ' << diagnostic.location;
            }
            m_error << ": " << diagnostic.message << '\n';
        }
    }
}

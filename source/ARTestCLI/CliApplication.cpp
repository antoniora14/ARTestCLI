#include "CliApplication.h"

#include "ConsoleEventSink.h"
#include "ConsoleExecutionControl.h"
#include "ConsoleCancellationHandler.h"
#include "ARTestEngineClient.h"
#include "../ThirdParty/json.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <utility>

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
            if (command == "extension-run")
            {
                return RunExtensionCommand(arguments);
            }
            if (command == "extensions")
            {
                return RunCatalogCommand(arguments);
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
            std::string extensionRoot;
            for (std::size_t index = 2; index < arguments.size(); ++index)
            {
                if (arguments[index] == "--extensions")
                {
                    if (!extensionRoot.empty() || ++index >= arguments.size())
                        throw std::invalid_argument("--extensions requires one catalog root.");
                    extensionRoot = arguments[index];
                }
                else if (command == "break")
                {
                    std::size_t parsedCharacters = 0;
                    const auto breakpoint = std::stoull(arguments[index], &parsedCharacters);
                    if (parsedCharacters != arguments[index].size())
                    {
                        throw std::invalid_argument("Breakpoint values must be non-negative integers.");
                    }
                    breakpoints.insert(static_cast<std::size_t>(breakpoint));
                }
                else throw std::invalid_argument("Unexpected command-line arguments.");
            }

            return RunPlan(
                command, arguments[1], std::move(breakpoints), extensionRoot);
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

    int CliApplication::RunCatalogCommand(
        const std::vector<std::string>& arguments)
    {
        if (arguments.size() != 3
            || (arguments[1] != "list"
                && arguments[1] != "validate"
                && arguments[1] != "doctor"))
        {
            m_error << "Usage: ARTestCLI extensions <list|validate|doctor> "
                    << "<extensions-root>\n";
            return static_cast<int>(ExitCode::InvalidArguments);
        }

        sdk::EngineClient engine;
        auto status = engine.Create("{\"loadDefaultCatalog\":false}");
        if (!status.Succeeded())
        {
            PrintEngineFailure("create", status.code, status.message);
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }

        const auto& operation = arguments[1];
        std::string report;
        if (operation == "doctor")
        {
            status = engine.SubscribeEvents([this](std::string_view eventText)
            {
                const auto event = nlohmann::json::parse(eventText);
                m_output << "[Engine] "
                         << event.value("source", std::string{}) << ": "
                         << event.value("message", std::string{}) << '\n';
            });
            if (status.Succeeded()) status = engine.RefreshCatalog(arguments[2]);

            // A rejected activation still publishes its complete diagnostic snapshot.
            const auto snapshotStatus = engine.GetCatalogSnapshot(report);
            if (!snapshotStatus.Succeeded())
            {
                PrintEngineFailure("catalog snapshot", snapshotStatus.code,
                    snapshotStatus.message);
                return static_cast<int>(ExitCode::UnexpectedFailure);
            }
            m_output << nlohmann::json::parse(report).dump(2) << '\n';
            if (!status.Succeeded())
            {
                PrintEngineFailure("catalog activation", status.code, status.message);
                return static_cast<int>(ExitCode::ExtensionCatalogInvalid);
            }
            return static_cast<int>(ExitCode::Success);
        }

        status = engine.ValidateCatalog(arguments[2], report);
        if (!status.Succeeded())
        {
            PrintEngineFailure("catalog validation", status.code, status.message);
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }
        const auto catalog = nlohmann::json::parse(report);
        if (operation == "validate")
        {
            m_output << catalog.dump(2) << '\n';
        }
        else
        {
            for (const auto& package : catalog.at("packages"))
            {
                m_output << (package.value("valid", false) ? "VALID   " : "INVALID ")
                         << package.value("extensionId", std::string{"<unknown>"})
                         << "  " << package.value("version", std::string{})
                         << "  sha256="
                         << package.value("integrity", std::string{"notDeclared"})
                         << '\n';
            }
            m_output << catalog.at("packages").size() << " package(s), catalog "
                     << (catalog.value("valid", false) ? "valid" : "invalid")
                     << ".\n";
        }
        return catalog.value("valid", false)
            ? static_cast<int>(ExitCode::Success)
            : static_cast<int>(ExitCode::ExtensionCatalogInvalid);
    }

    int CliApplication::RunExtensionCommand(
        const std::vector<std::string>& arguments)
    {
        if (arguments.size() != 3)
        {
            m_error << "Usage: ARTestCLI extension-run <script.json> <extensions-root>\n";
            return static_cast<int>(ExitCode::InvalidArguments);
        }
        return RunPlan("run", arguments[1], {}, arguments[2], true);
    }

    int CliApplication::RunPlan(
        const std::string& command,
        const std::string& scriptPath,
        std::unordered_set<std::size_t> breakpoints,
        const std::string& extensionRoot,
        bool compatibilityJson)
    {
        std::string content;
        if (!ReadScript(scriptPath, content))
            return static_cast<int>(ExitCode::InvalidScript);

        sdk::EngineClient engine;
        auto status = engine.Create(extensionRoot.empty() ? "{}" : "{\"loadDefaultCatalog\":false}");
        if (!status.Succeeded())
        {
            PrintEngineFailure("create", status.code, status.message);
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }

        ConsoleEventSink eventSink(m_output, m_error);
        if (!extensionRoot.empty())
        {
            status = engine.PrepareCatalog(extensionRoot);
            if (!status.Succeeded())
            {
                PrintEngineFailure("catalog preparation", status.code, status.message);
                return static_cast<int>(ExitCode::ExtensionCatalogInvalid);
            }
        }
        if (command != "compile")
        {
            status = engine.SubscribeEvents(
                [this, &eventSink, compatibilityJson](std::string_view eventText)
                {
                    if (compatibilityJson)
                    {
                        const auto event = nlohmann::json::parse(eventText);
                        m_output << "[Engine] " << event.value("source", std::string{})
                            << ": " << event.value("message", std::string{}) << '\n';
                    }
                    else eventSink.Publish(eventText);
                });
            if (!status.Succeeded())
            {
                PrintEngineFailure("subscribe", status.code, status.message);
                return static_cast<int>(ExitCode::UnexpectedFailure);
            }
        }

        std::string compileReport;
        status = engine.CompileDetailed(content, compileReport);
        if (!status.Succeeded())
        {
            PrintEngineFailure("compile", status.code, status.message);
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }
        const auto compilation = nlohmann::json::parse(compileReport);
        if (!compilation.value("valid", false))
        {
            PrintCompileDiagnostics(compileReport, scriptPath);
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
        if (command == "debug" || command == "break")
        {
            status = engine.Start(
                [&control](const sdk::StepExecutionInfo& step)
                {
                    return control.BeforeStep(step);
                });
        }
        else
        {
            status = engine.Start();
        }
        if (!status.Succeeded())
        {
            PrintEngineFailure("start", status.code, status.message);
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }

        bool completed = false;
        {
            ConsoleCancellationHandler cancellationHandler(engine);
            status = engine.Wait(UINT32_MAX, completed);
        }
        if (!status.Succeeded() || !completed)
        {
            PrintEngineFailure("wait", status.code, status.message);
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }

        std::string runReport;
        status = engine.SerializeResult(runReport);
        if (!status.Succeeded())
        {
            PrintEngineFailure("result", status.code, status.message);
            return static_cast<int>(ExitCode::UnexpectedFailure);
        }
        const auto result = nlohmann::json::parse(runReport);
        if (compatibilityJson) m_output << runReport << '\n';
        if (result.value("status", std::string{}) == "passed")
            return static_cast<int>(ExitCode::Success);
        if (result.value("failureKind", 4) == 1)
            return static_cast<int>(ExitCode::InstrumentInitializationFailed);
        return static_cast<int>(ExitCode::ExecutionFailed);
    }

    bool CliApplication::ReadScript(
        const std::string& scriptPath,
        std::string& content) const
    {
        std::ifstream input{std::filesystem::path{scriptPath}, std::ios::binary};
        if (!input)
        {
            m_error << "The JSON script could not be opened.\n";
            return false;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        content = std::move(buffer).str();
        return true;
    }

    void CliApplication::PrintHelp() const
    {
        m_output
            << "Usage:\n"
            << "  ARTestCLI <command> <script.json> [options]\n\n"
            << "  --extensions <root>     Use an explicit catalog for compile/run/debug/break.\n"
            << "Commands:\n"
            << "  compile                 Validate the script without initializing instruments.\n"
            << "  run                     Execute the complete script.\n"
            << "  debug                   Pause before every step.\n"
            << "  break idx1 idx2 ...     Pause at zero-based command indices.\n"
            << "  extension-run           Compatibility alias for run with final JSON output.\n"
            << "  extensions list         List packages after safe manifest validation.\n"
            << "  extensions validate     Emit the machine-readable validation report without loading DLLs.\n"
            << "  extensions doctor       Validate, load, inspect ABI descriptors, and activate a catalog.\n"
            << "  help                    Display this help.\n\n"
            << "During run, debug, or break, press Ctrl+C to request cooperative cancellation.\n"
            << "The engine always attempts instrument cleanup before returning.\n\n"
            << "Examples:\n"
            << "  ARTestCLI compile source/Scripts/TestScript.json\n"
            << "  ARTestCLI run source/Scripts/TestScript.json\n"
            << "  ARTestCLI debug source/Scripts/TestScript.json\n"
            << "  ARTestCLI break source/Scripts/TestScript.json 1 3\n";
        m_output
            << "  ARTestCLI extension-run source/Scripts/ExtensionScript.json "
            << "artifacts/extensions/x64/Release\n";
        m_output
            << "  ARTestCLI extensions validate artifacts/extensions/x64/Release\n"
            << "  ARTestCLI extensions doctor artifacts/extensions/x64/Release\n";
    }

    void CliApplication::PrintCompileDiagnostics(
        std::string_view reportJson,
        std::string_view scriptPath) const
    {
        const auto report = nlohmann::json::parse(reportJson);
        for (const auto& diagnostic : report.at("diagnostics"))
        {
            m_error << '[' << diagnostic.value("code", std::string{"UNKNOWN"}) << ']';
            auto location = diagnostic.value("location", std::string{});
            if (location == "engine-api") location = scriptPath;
            if (!location.empty())
            {
                m_error << ' ' << location;
            }
            m_error << ": " << diagnostic.value("message", std::string{}) << '\n';
        }
    }

    void CliApplication::PrintEngineFailure(
        std::string_view operation,
        int code,
        std::string_view message) const
    {
        m_error << "ARTestEngine";
        if (!operation.empty()) m_error << ' ' << operation;
        m_error << " failure [" << code << "]: " << message << '\n';
    }
}

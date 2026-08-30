// ARTestCLI.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <iostream>
#include <vector>
#include <string>

#include "ScriptExecutor.h"
#include "CCommandFactory.h"
#include "ScriptDocument.h"
#include "../ArtInstruments/InstrumentFactory.h"

namespace
{
    enum class ExitCode
    {
        Success = 0,
        InvalidArguments = 2,
        InvalidScript = 3,
        InstrumentInitializationFailed = 4,
        ExecutionFailed = 5,
        UnexpectedFailure = 10
    };

    void PrintDiagnostics(const std::vector<Diagnostic>& diagnostics)
    {
        for (const auto& diagnostic : diagnostics)
        {
            std::cerr << "[" << diagnostic.code << "]";
            if (!diagnostic.location.empty())
            {
                std::cerr << " " << diagnostic.location;
            }
            std::cerr << ": " << diagnostic.message << "\n";
        }
    }
}
// ----------------------------------------------------------------
// Print help
// ----------------------------------------------------------------
void PrintHelp()
{
    std::cout <<
        "Uso:\n"
        "  artseq <comando> <script.json> [opciones]\n\n"
        "Comandos:\n"
        "  compile                 Valida el script y sale.\n"
        "  run                     Ejecuta el script completo.\n"
        "  debug                   Paso a paso (break en cada paso).\n"
        "  break idx1 idx2 ...     Ejecuta con breakpoints en los índices dados.\n"
        "  help                    Muestra esta ayuda.\n\n"
        "Ejemplos:\n"
        "  artseq compile scripts/test.json\n"
        "  artseq run     scripts/test.json\n"
        "  artseq debug   scripts/test.json\n"
        "  artseq break   scripts/test.json 2 5\n";
}


int main(int argc, char* argv[])
{
    try
    {
        if (argc < 2)
        {
            PrintHelp();
            return static_cast<int>(ExitCode::InvalidArguments);
        }

        const std::string command = argv[1];
        if (command == "help")
        {
            PrintHelp();
            return static_cast<int>(ExitCode::Success);
        }

        if (command != "compile" && command != "run" && command != "debug" && command != "break")
        {
            std::cerr << "Unknown command: " << command << "\n";
            PrintHelp();
            return static_cast<int>(ExitCode::InvalidArguments);
        }

        if (argc < 3)
        {
            std::cerr << "Error: missing JSON script path.\n";
            return static_cast<int>(ExitCode::InvalidArguments);
        }

        const std::string scriptPath = argv[2];
        auto scriptResult = ScriptDocumentLoader::Load(scriptPath);
        if (!scriptResult.Succeeded())
        {
            PrintDiagnostics(scriptResult.diagnostics);
            return static_cast<int>(ExitCode::InvalidScript);
        }

        InstrumentFactory instrumentManager;
        OperationResult definitionsResult = instrumentManager.LoadDefinitions(scriptResult.value->instruments);
        if (!definitionsResult.Succeeded())
        {
            PrintDiagnostics(definitionsResult.diagnostics);
            return static_cast<int>(ExitCode::InvalidScript);
        }

        auto commandsResult = CommandFactory::CreateCommands(
            scriptResult.value->commands,
            instrumentManager);
        if (!commandsResult.Succeeded())
        {
            PrintDiagnostics(commandsResult.diagnostics);
            return static_cast<int>(ExitCode::InvalidScript);
        }

        CScriptExecutor executor(std::move(*commandsResult.value));
        OperationResult compilation = executor.Compile();
        if (!compilation.Succeeded())
        {
            PrintDiagnostics(compilation.diagnostics);
            return static_cast<int>(ExitCode::InvalidScript);
        }

        if (command == "compile")
        {
            std::cout << "Valid script. No instruments were initialized.\n";
            return static_cast<int>(ExitCode::Success);
        }

        if (command == "debug")
        {
            executor.SetInteractiveMode(true);
        }
        else if (command == "break")
        {
            for (int index = 3; index < argc; ++index)
            {
                std::size_t parsedCharacters = 0;
                const unsigned long long breakpoint = std::stoull(argv[index], &parsedCharacters);
                if (parsedCharacters != std::string{argv[index]}.size())
                {
                    throw std::invalid_argument("Breakpoint values must be non-negative integers.");
                }
                executor.AddBreakpoint(static_cast<std::size_t>(breakpoint));
            }
        }

        OperationResult initialization = instrumentManager.InitializeAll();
        if (!initialization.Succeeded())
        {
            PrintDiagnostics(initialization.diagnostics);
            return static_cast<int>(ExitCode::InstrumentInitializationFailed);
        }

        const RunResult run = executor.Execute();
        return static_cast<int>(run.Succeeded() ? ExitCode::Success : ExitCode::ExecutionFailed);
    }
    catch (const std::invalid_argument& exception)
    {
        std::cerr << "Invalid argument: " << exception.what() << "\n";
        return static_cast<int>(ExitCode::InvalidArguments);
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Unexpected failure: " << exception.what() << "\n";
        return static_cast<int>(ExitCode::UnexpectedFailure);
    }
    catch (...)
    {
        std::cerr << "Unexpected non-standard failure.\n";
        return static_cast<int>(ExitCode::UnexpectedFailure);
    }
}

// ARTestCLI.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

#include "../ArtCore/ScriptExecutor.h"
#include "../ArtCore/CCommandFactory.h"
#include "../ArtInstruments/InstrumentFactory.h";


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
    if (argc < 2) { PrintHelp(); return 1; }

    std::string command = argv[1];
    if (command == "help") { PrintHelp(); return 0; }

    if (argc < 3) 
    {
        std::cerr << "Error: falta el archivo JSON.\n";
        return 1;
    }
    
    // 1) Get script file .JSON path
    std::string scriptPath = argv[2];

    InstrumentFactory InstrumentManager;
    if (InstrumentManager.LoadInstruments(scriptPath) == false)
    {
        std::cerr << "Unable to initialize instruments.\n";
        return -1;
    }

    // 2) Load JSON file and create commands with factory 
    auto cmdVector = CommandFactory::LoadScript(scriptPath, InstrumentManager);
    if (cmdVector.empty()) 
    {
        std::cerr << "The script contain invalid commands.\n";
        return -1;
    }

    // 3) Create Test xecutor with commands
    CScriptExecutor executor(std::move(cmdVector));

    // 4) Compile (validate)
    std::vector<std::string> errors;
    if (!executor.Compile(errors)) 
    {
        std::cout << " Compilation errors: \n";
        for (const auto& e : errors) std::cout << " - " << e << '\n';
        return 1;
    }
    if (command == "compile") 
    {
        std::cout << " Valid script.\n";
        return 0;
    }

    // 5) Configure breakpoints or step-by-step mode
    if (command == "debug")
    {
        // pause on each step
        executor.SetInteractiveMode(true);            
    }
    else if (command == "break")
    {
        for (int i = 3; i < argc; ++i) 
        {
            size_t idx = std::stoul(argv[i]);
            executor.AddBreakpoint(idx);
        }
    }
    else if (command != "run") 
    {
        std::cerr << "Unknown command: " << command << "\n";
        PrintHelp();
        return 1;
    }

    // 6) Execute
    executor.Execute();
    return 0;
}



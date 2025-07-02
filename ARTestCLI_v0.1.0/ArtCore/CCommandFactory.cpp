
#include "CCommandFactory.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;


std::unordered_map<std::string, CommandFactory::CreatorTestFunction>& CommandFactory::GetRegistry()
{
	static std::unordered_map<std::string, CreatorTestFunction> registry;
	return registry;
}


void CommandFactory::RegisterCommand(const std::string& CmdType, CreatorTestFunction CmdCreator)
{
	GetRegistry()[CmdType] = std::move(CmdCreator);
}


std::vector<std::unique_ptr<ICommand>> CommandFactory::LoadScript(const std::string& ScriptName, InstrumentFactory& instrumentManager)
{
	std::cout << "-> Loading script from file: " << ScriptName << std::endl;
	std::vector<std::unique_ptr<ICommand>> ArtCommands;

	std::ifstream scriptFile(ScriptName);
	if (!scriptFile.is_open())
	{
		std::cerr << "Error opening script file: " << ScriptName << "\n";
		return{};
	}

	json scriptJson;
	try
	{
		scriptFile >> scriptJson;

		for (const auto& command : scriptJson["commands"])
		{
			std::string type = command["name"];
			const auto& params = command["params"];
			std::string instrumentId = command["instrument"];

			auto instrument = instrumentManager.GetInstrument(instrumentId);
			if (!instrument && (instrumentId.compare("NoInstrument") != 0))
			{
				std::cerr << "[CommandFactory] Unknown instrument ID: " << instrumentId << std::endl;
				continue;
			}

			auto TestCmd = GetRegistry().find(type);
			if (TestCmd != GetRegistry().end())
			{
				auto cmd = TestCmd->second();
				cmd->Configure(params, instrument);
				ArtCommands.push_back(std::move(cmd));
			}
			else
			{
				std::cerr << "Unknown Command type: " << type << "\n";
			}
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error parsering JSON: " << e.what() << "\n";
		return {};
	}

	if(scriptFile.is_open()) scriptFile.close();

	return ArtCommands;
}



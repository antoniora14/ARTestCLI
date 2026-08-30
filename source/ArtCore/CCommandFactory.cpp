
#include "CCommandFactory.h"
#include <unordered_set>

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


ValueResult<std::vector<CommandInstance>> CommandFactory::CreateCommands(
	const nlohmann::json& commandDefinitions,
	InstrumentFactory& instrumentManager)
{
	ValueResult<std::vector<CommandInstance>> result;
	std::vector<CommandInstance> commands;
	std::unordered_set<std::uint64_t> stepIds;

	if (commandDefinitions.empty())
	{
		result.diagnostics.push_back({DiagnosticSeverity::Error, "SCRIPT_EMPTY", "The script must contain at least one command.", "commands"});
		return result;
	}

	for (std::size_t index = 0; index < commandDefinitions.size(); ++index)
	{
		const auto& command = commandDefinitions[index];
		const std::string location = "commands[" + std::to_string(index) + "]";
		try
		{
			if (!command.is_object()
				|| !command.contains("stepId") || !command["stepId"].is_number_integer()
				|| !command.contains("name") || !command["name"].is_string()
				|| !command.contains("instrument") || !command["instrument"].is_string()
				|| !command.contains("params") || !command["params"].is_object())
			{
				result.diagnostics.push_back({DiagnosticSeverity::Error, "COMMAND_SCHEMA_INVALID", "The command definition is incomplete or has invalid types.", location});
				continue;
			}

			std::uint64_t stepId = 0;
			if (command["stepId"].is_number_unsigned())
			{
				stepId = command["stepId"].get<std::uint64_t>();
			}
			else
			{
				const std::int64_t signedStepId = command["stepId"].get<std::int64_t>();
				if (signedStepId > 0)
				{
					stepId = static_cast<std::uint64_t>(signedStepId);
				}
			}
			if (stepId == 0 || !stepIds.insert(stepId).second)
			{
				result.diagnostics.push_back({DiagnosticSeverity::Error, "COMMAND_STEP_ID_INVALID", "Step identifiers must be unique positive integers.", location});
				continue;
			}

			const std::string type = command["name"].get<std::string>();
			const auto& params = command["params"];
			const std::string instrumentId = command["instrument"].get<std::string>();

			auto instrument = instrumentManager.GetInstrument(instrumentId);
			if (!instrument && instrumentId != "NoInstrument")
			{
				result.diagnostics.push_back({DiagnosticSeverity::Error, "COMMAND_INSTRUMENT_UNKNOWN", "Unknown instrument ID: " + instrumentId, location});
				continue;
			}

			auto commandType = GetRegistry().find(type);
			if (commandType != GetRegistry().end())
			{
				auto cmd = commandType->second();
				cmd->Configure(params, instrument);
				commands.push_back({stepId, std::move(cmd)});
			}
			else
			{
				result.diagnostics.push_back({DiagnosticSeverity::Error, "COMMAND_TYPE_UNKNOWN", "Unknown command type: " + type, location});
			}
		}
		catch (const std::exception& exception)
		{
			result.diagnostics.push_back({DiagnosticSeverity::Error, "COMMAND_CONFIGURATION_INVALID", exception.what(), location});
		}
	}

	if (!ContainsErrors(result.diagnostics))
	{
		result.value = std::move(commands);
	}
	return result;
}

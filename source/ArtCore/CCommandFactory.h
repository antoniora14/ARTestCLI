#pragma once
#include "ICommands.h"
#include "Diagnostics.h"
#include "../ThirdParty/json.hpp"
#include"../ArtInstruments/InstrumentFactory.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


class CommandFactory
{
public:
	using CreatorTestFunction = std::function<std::unique_ptr<ICommand>()>;

	static ValueResult<std::vector<CommandInstance>> CreateCommands(
		const nlohmann::json& commandDefinitions,
		InstrumentFactory& instrumentManager);
	static void RegisterCommand(const std::string& CmdType, CreatorTestFunction CmdCreator);

private:
	static std::unordered_map<std::string, CreatorTestFunction>& GetRegistry();
};


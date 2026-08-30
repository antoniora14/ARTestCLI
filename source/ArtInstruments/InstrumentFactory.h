#pragma once
#include "IInstrument.h"
#include "../ThirdParty/json.hpp"
#include "../ArtCore/Diagnostics.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


class InstrumentFactory
{
public:
	using CreatorTestInstrument = std::function<std::unique_ptr<IInstrument>()>;
	~InstrumentFactory();

	[[nodiscard]] OperationResult LoadDefinitions(const nlohmann::json& instrumentDefinitions);
	[[nodiscard]] OperationResult InitializeAll();
	void ShutdownAll() noexcept;
	std::shared_ptr<IInstrument> GetInstrument(const std::string& id) const;
	static void RegisterInstrument(const std::string& type, CreatorTestInstrument Instcreator);

private:
	static std::unordered_map<std::string, CreatorTestInstrument>& GetRegistry();
	struct InstrumentEntry
	{
		std::shared_ptr<IInstrument> instrument;
		nlohmann::json configuration;
		bool initialized = false;
	};
	std::map<std::string, InstrumentEntry> m_instruments;
};


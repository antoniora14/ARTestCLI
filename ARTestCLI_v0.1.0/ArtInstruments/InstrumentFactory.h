#pragma once
#include "IInstrument.h"
#include "../ThirdParty/json.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


class InstrumentFactory
{
public:
	using CreatorTestInstrument = std::function<std::unique_ptr<IInstrument>()>;

	bool LoadInstruments(const std::string& filename);
	std::shared_ptr<IInstrument> GetInstrument(const std::string& id) const;
	static void RegisterInstrument(const std::string& type, CreatorTestInstrument Instcreator);

private:
	static std::unordered_map<std::string, CreatorTestInstrument>& GetRegistry();
	std::map<std::string, std::shared_ptr<IInstrument>> m_pInstrumentList;
};



#include "InstrumentFactory.h"
#include "../ArtUtils/ArtMacros.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;


std::unordered_map<std::string, InstrumentFactory::CreatorTestInstrument>& InstrumentFactory::GetRegistry()
{
    static std::unordered_map<std::string, CreatorTestInstrument> registry;
    return registry;
}


void InstrumentFactory::RegisterInstrument(const std::string& type, CreatorTestInstrument Instcreator)
{
    GetRegistry()[type] = std::move(Instcreator);
}


std::shared_ptr<IInstrument> InstrumentFactory::GetInstrument(const std::string& id) const
{
    auto it = m_pInstrumentList.find(id);
    if (it != m_pInstrumentList.end()) return it->second;
    return nullptr;
}


bool InstrumentFactory::LoadInstruments(const std::string& filename)
{
    std::cout << "-> Loading instrtuments from file: " << filename << std::endl;
    if(m_pInstrumentList.empty() == false) m_pInstrumentList.clear();

    std::ifstream scriptFile(filename);
    if (!scriptFile.is_open())
    {
        std::cerr << "Error opening script file: " << filename << "\n";
        return false;
    }
    json scriptJson;

    try
    {
        scriptFile >> scriptJson;

        for (const auto& JsonItem : scriptJson["instruments"])
        {
            std::string Type = JsonItem["type"];
            std::string ID = JsonItem["id"];
            const auto& ConfigParms = JsonItem["config"];

            auto TestInstrument = GetRegistry().find(Type);
            if (TestInstrument != GetRegistry().end())
            {
                auto instru = TestInstrument->second();
                CHECK_SMART_PTR(instru);
                instru->Initialize(ConfigParms);
                m_pInstrumentList[ID] = std::shared_ptr<IInstrument>(std::move(instru));
            }
            else
            {
                std::cerr << "Unknown Instrument type: " << Type << "\n";
                return false;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error parsering JSON: " << e.what() << "\n";
        return false;
    }

    if (scriptFile.is_open()) scriptFile.close();

    return true;
}




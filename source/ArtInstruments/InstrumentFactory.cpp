
#include "InstrumentFactory.h"
#include <unordered_set>

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
	auto it = m_instruments.find(id);
	if (it != m_instruments.end()) return it->second.instrument;
	return nullptr;
}
InstrumentFactory::~InstrumentFactory()
{
    ShutdownAll();
}

OperationResult InstrumentFactory::LoadDefinitions(const nlohmann::json& instrumentDefinitions)
{
    OperationResult result;
    std::map<std::string, InstrumentEntry> loaded;

    for (std::size_t index = 0; index < instrumentDefinitions.size(); ++index)
    {
        const auto& definition = instrumentDefinitions[index];
        const std::string location = "instruments[" + std::to_string(index) + "]";
        try
        {
            if (!definition.is_object()
                || !definition.contains("type") || !definition["type"].is_string()
                || !definition.contains("id") || !definition["id"].is_string()
                || !definition.contains("config") || !definition["config"].is_object())
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error, "INSTRUMENT_SCHEMA_INVALID", "The instrument definition is incomplete or has invalid types.", location});
                continue;
            }

            const std::string type = definition["type"].get<std::string>();
            const std::string id = definition["id"].get<std::string>();
            if (id.empty() || loaded.contains(id))
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error, "INSTRUMENT_ID_INVALID", "Instrument identifiers must be non-empty and unique.", location});
                continue;
            }

            auto instrumentType = GetRegistry().find(type);
            if (instrumentType != GetRegistry().end())
            {
                auto instrument = instrumentType->second();
                if (!instrument)
                {
                    result.diagnostics.push_back({DiagnosticSeverity::Error, "INSTRUMENT_CREATION_FAILED", "The instrument factory returned a null instance.", location});
                    continue;
                }
                instrument->SetId(id);
                loaded.emplace(id, InstrumentEntry{std::shared_ptr<IInstrument>{std::move(instrument)}, definition["config"], false});
            }
            else
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error, "INSTRUMENT_TYPE_UNKNOWN", "Unknown instrument type: " + type, location});
            }
        }
        catch (const std::exception& exception)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, "INSTRUMENT_CONFIGURATION_INVALID", exception.what(), location});
        }
    }

    if (result.Succeeded())
    {
        ShutdownAll();
        m_instruments = std::move(loaded);
    }
    return result;
}

OperationResult InstrumentFactory::InitializeAll()
{
    OperationResult result;
    for (auto& [id, entry] : m_instruments)
    {
        try
        {
            OperationResult initialization = entry.instrument->Initialize(entry.configuration);
            if (!initialization.Succeeded())
            {
                for (auto& diagnostic : initialization.diagnostics)
                {
                    if (diagnostic.location.empty())
                    {
                        diagnostic.location = "instrument=" + id;
                    }
                    result.diagnostics.push_back(std::move(diagnostic));
                }
                break;
            }
            entry.initialized = true;
        }
        catch (const std::exception& exception)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, "INSTRUMENT_INITIALIZATION_EXCEPTION", exception.what(), "instrument=" + id});
            break;
        }
        catch (...)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, "INSTRUMENT_INITIALIZATION_EXCEPTION", "Unknown exception while initializing the instrument.", "instrument=" + id});
            break;
        }
    }

    if (!result.Succeeded())
    {
        ShutdownAll();
    }
    return result;
}

void InstrumentFactory::ShutdownAll() noexcept
{
    for (auto iterator = m_instruments.rbegin(); iterator != m_instruments.rend(); ++iterator)
    {
        if (iterator->second.initialized)
        {
            try
            {
                iterator->second.instrument->Shutdown();
            }
            catch (...)
            {
            }
            iterator->second.initialized = false;
        }
    }
}

#include "InstrumentManager.h"

#include <exception>
#include <utility>

namespace artest
{
    InstrumentManager::InstrumentManager(InstrumentRegistry& registry, IEventSink& eventSink)
        : m_registry(registry), m_eventSink(eventSink)
    {
    }

    InstrumentManager::~InstrumentManager()
    {
        ShutdownAll();
    }

    OperationResult InstrumentManager::LoadDefinitions(
        const std::vector<InstrumentDefinition>& definitions)
    {
        OperationResult result;
        std::map<std::string, InstrumentEntry> loaded;

        for (std::size_t index = 0; index < definitions.size(); ++index)
        {
            const auto& definition = definitions[index];
            const std::string location = "instruments[" + std::to_string(index) + "]";

            if (definition.id.empty() || loaded.contains(definition.id))
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "INSTRUMENT_ID_INVALID",
                    "Instrument identifiers must be non-empty and unique.",
                    location});
                continue;
            }

            try
            {
                auto instrument = m_registry.Create(definition.type, m_eventSink);
                if (!instrument)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error,
                        "INSTRUMENT_TYPE_UNKNOWN",
                        "Unknown instrument type: " + definition.type,
                        location});
                    continue;
                }

                instrument->SetId(definition.id);
                loaded.emplace(
                    definition.id,
                    InstrumentEntry{
                        std::shared_ptr<IInstrument>{std::move(instrument)},
                        definition.configuration,
                        false});
            }
            catch (const std::exception& exception)
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "INSTRUMENT_CREATION_EXCEPTION",
                    exception.what(),
                    location});
            }
            catch (...)
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "INSTRUMENT_CREATION_EXCEPTION",
                    "Unknown exception while creating the instrument.",
                    location});
            }
        }

        if (result.Succeeded())
        {
            ShutdownAll();
            m_instruments = std::move(loaded);
        }
        return result;
    }

    OperationResult InstrumentManager::InitializeAll()
    {
        OperationResult result;
        m_initializationOrder.clear();

        for (auto& [id, entry] : m_instruments)
        {
            m_eventSink.Publish({
                EngineEventKind::InstrumentInitializing,
                EngineEventSeverity::Information,
                id,
                "Initializing instrument."});

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
                m_initializationOrder.push_back(id);
                m_eventSink.Publish({
                    EngineEventKind::InstrumentInitialized,
                    EngineEventSeverity::Information,
                    id,
                    "Instrument initialized."});
            }
            catch (const std::exception& exception)
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "INSTRUMENT_INITIALIZATION_EXCEPTION",
                    exception.what(),
                    "instrument=" + id});
                break;
            }
            catch (...)
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "INSTRUMENT_INITIALIZATION_EXCEPTION",
                    "Unknown exception while initializing the instrument.",
                    "instrument=" + id});
                break;
            }
        }

        if (!result.Succeeded())
        {
            ShutdownAll();
        }
        return result;
    }

    void InstrumentManager::ShutdownAll() noexcept
    {
        for (auto iterator = m_initializationOrder.rbegin();
             iterator != m_initializationOrder.rend();
             ++iterator)
        {
            const auto entry = m_instruments.find(*iterator);
            if (entry == m_instruments.end() || !entry->second.initialized)
            {
                continue;
            }

            try
            {
                entry->second.instrument->Shutdown();
            }
            catch (...)
            {
            }
            entry->second.initialized = false;
            m_eventSink.Publish({
                EngineEventKind::InstrumentShutdown,
                EngineEventSeverity::Information,
                entry->first,
                "Instrument shut down."});
        }
        m_initializationOrder.clear();
    }

    std::shared_ptr<IInstrument> InstrumentManager::GetInstrument(const std::string& id) const
    {
        const auto instrument = m_instruments.find(id);
        return instrument == m_instruments.end() ? nullptr : instrument->second.instrument;
    }
}

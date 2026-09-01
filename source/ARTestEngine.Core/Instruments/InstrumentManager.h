#pragma once

#include "IInstrument.h"
#include "InstrumentRegistry.h"
#include "../Diagnostics.h"
#include "../Execution/IEventSink.h"
#include "../Model/TestPlan.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace artest
{
    class InstrumentManager
    {
    public:
        InstrumentManager(InstrumentRegistry& registry, IEventSink& eventSink);
        ~InstrumentManager();

        InstrumentManager(const InstrumentManager&) = delete;
        InstrumentManager& operator=(const InstrumentManager&) = delete;

        [[nodiscard]] OperationResult LoadDefinitions(
            const std::vector<InstrumentDefinition>& definitions);
        [[nodiscard]] OperationResult InitializeAll();
        [[nodiscard]] OperationResult ShutdownAll();
        [[nodiscard]] std::shared_ptr<IInstrument> GetInstrument(const std::string& id) const;

    private:
        struct InstrumentEntry
        {
            std::shared_ptr<IInstrument> instrument;
            nlohmann::json configuration;
            bool initialized = false;
        };

        InstrumentRegistry& m_registry;
        IEventSink& m_eventSink;
        std::map<std::string, InstrumentEntry> m_instruments;
        std::vector<std::string> m_initializationOrder;
    };
}

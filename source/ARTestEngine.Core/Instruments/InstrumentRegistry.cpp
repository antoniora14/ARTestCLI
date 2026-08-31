#include "InstrumentRegistry.h"

#include <mutex>
#include <utility>

namespace artest
{
    OperationResult InstrumentRegistry::Register(std::string instrumentType, Creator creator)
    {
        if (instrumentType.empty() || !creator)
        {
            return OperationResult::Failure(
                "INSTRUMENT_REGISTRATION_INVALID",
                "Instrument type and creator are required.");
        }

        std::unique_lock lock{m_mutex};
        if (m_creators.contains(instrumentType))
        {
            return OperationResult::Failure(
                "INSTRUMENT_REGISTRATION_DUPLICATE",
                "An instrument with this type is already registered.",
                instrumentType);
        }

        m_creators.emplace(std::move(instrumentType), std::move(creator));
        return OperationResult::Success();
    }

    std::unique_ptr<IInstrument> InstrumentRegistry::Create(
        const std::string& instrumentType,
        IEventSink& eventSink) const
    {
        std::shared_lock lock{m_mutex};
        const auto instrument = m_creators.find(instrumentType);
        return instrument == m_creators.end() ? nullptr : instrument->second(eventSink);
    }

    bool InstrumentRegistry::Contains(const std::string& instrumentType) const
    {
        std::shared_lock lock{m_mutex};
        return m_creators.contains(instrumentType);
    }
}

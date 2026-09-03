#pragma once

#include "IInstrument.h"
#include "../Diagnostics.h"
#include "../Execution/IEventSink.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace artest
{
    class RegistryTransaction;
    class RegistrationOwner;
    class InstrumentRegistry
    {
    public:
        using Creator = std::function<std::unique_ptr<IInstrument>(IEventSink&)>;

        [[nodiscard]] OperationResult Register(std::string instrumentType, Creator creator);
        [[nodiscard]] std::unique_ptr<IInstrument> Create(
            const std::string& instrumentType,
            IEventSink& eventSink) const;
        [[nodiscard]] bool Contains(const std::string& instrumentType) const;

    private:
        friend class RegistryTransaction;
        std::unordered_map<std::string, std::shared_ptr<const RegistrationOwner>> m_owners;
        mutable std::shared_mutex m_mutex;
        std::unordered_map<std::string, Creator> m_creators;
    };
}

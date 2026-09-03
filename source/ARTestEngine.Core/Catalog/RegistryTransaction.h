#pragma once
#include "../Commands/CommandRegistry.h"
#include "../Instruments/InstrumentRegistry.h"
#include <mutex>
#include <vector>

namespace artest
{
class RegistrationOwner final
{
    RegistrationOwner() = default;
    friend class RegistryTransaction;
};
using RegistrationToken = std::shared_ptr<const RegistrationOwner>;

class RegistryTransaction final
{
  public:
    struct Command
    {
        std::string id;
        CommandRegistry::Creator create;
    };
    struct Instrument
    {
        std::string id;
        InstrumentRegistry::Creator create;
    };

    [[nodiscard]] static ValueResult<RegistrationToken> Commit(
        CommandRegistry &commands, InstrumentRegistry &instruments,
        const std::vector<Command> &commandBatch, const std::vector<Instrument> &instrumentBatch)
    {
        // Both locks are held until the no-throw swaps finish. Readers can never
        // observe a partial batch, including when allocation or validation fails.
        std::scoped_lock lock{commands.m_mutex, instruments.m_mutex};
        auto nextCommands = commands.m_creators;
        auto nextInstruments = instruments.m_creators;
        auto commandOwners = commands.m_owners;
        auto instrumentOwners = instruments.m_owners;
        RegistrationToken owner{new RegistrationOwner};
        ValueResult<RegistrationToken> result;
        const auto conflict = [&result](const std::string &id) {
            result.diagnostics.push_back(
                {DiagnosticSeverity::Error, "EXTENSION_COMPONENT_DUPLICATE",
                 "The registration batch conflicts with an existing component.", id});
        };
        for (const auto &entry : commandBatch)
        {
            if (entry.id.empty() || !entry.create || nextInstruments.contains(entry.id) ||
                !nextCommands.emplace(entry.id, entry.create).second)
            {
                conflict(entry.id);
                return result;
            }
            commandOwners.emplace(entry.id, owner);
        }
        for (const auto &entry : instrumentBatch)
        {
            if (entry.id.empty() || !entry.create || nextCommands.contains(entry.id) ||
                !nextInstruments.emplace(entry.id, entry.create).second)
            {
                conflict(entry.id);
                return result;
            }
            instrumentOwners.emplace(entry.id, owner);
        }
        commands.m_creators.swap(nextCommands);
        instruments.m_creators.swap(nextInstruments);
        commands.m_owners.swap(commandOwners);
        instruments.m_owners.swap(instrumentOwners);
        result.value = std::move(owner);
        return result;
    }

    static void Revoke(CommandRegistry &commands, InstrumentRegistry &instruments,
                       const RegistrationToken &owner)
    {
        if (!owner)
            return;
        // Token identity, not a caller-supplied string, determines what may be removed.
        // Retire factories after releasing locks because their destructors may reenter.
        decltype(commands.m_creators) retiredCommands;
        decltype(instruments.m_creators) retiredInstruments;
        {
            std::scoped_lock lock{commands.m_mutex, instruments.m_mutex};
            // Reserve before extracting any node: allocation failure cannot partially revoke.
            retiredCommands.reserve(commands.m_creators.size());
            retiredInstruments.reserve(instruments.m_creators.size());
            for (auto it = commands.m_owners.begin(); it != commands.m_owners.end();)
                if (it->second == owner)
                {
                    retiredCommands.insert(commands.m_creators.extract(it->first));
                    it = commands.m_owners.erase(it);
                }
                else
                    ++it;
            for (auto it = instruments.m_owners.begin(); it != instruments.m_owners.end();)
                if (it->second == owner)
                {
                    retiredInstruments.insert(instruments.m_creators.extract(it->first));
                    it = instruments.m_owners.erase(it);
                }
                else
                    ++it;
        }
    }
};
} // namespace artest

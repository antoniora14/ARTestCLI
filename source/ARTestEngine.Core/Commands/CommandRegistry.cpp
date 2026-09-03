#include "CommandRegistry.h"

#include <mutex>
#include <utility>

namespace artest
{
    OperationResult CommandRegistry::Register(std::string commandName, Creator creator)
    {
        if (commandName.empty() || !creator)
        {
            return OperationResult::Failure(
                "COMMAND_REGISTRATION_INVALID",
                "Command name and creator are required.");
        }

        std::unique_lock lock{m_mutex};
        if (m_creators.contains(commandName))
        {
            return OperationResult::Failure(
                "COMMAND_REGISTRATION_DUPLICATE",
                "A command with this name is already registered.",
                commandName);
        }

        m_creators.emplace(std::move(commandName), std::move(creator));
        return OperationResult::Success();
    }

    std::unique_ptr<ICommand> CommandRegistry::Create(const std::string& commandName) const
    {
        Creator creator;
        {
            std::shared_lock lock{m_mutex};
            const auto command = m_creators.find(commandName);
            if (command != m_creators.end()) creator = command->second;
        }
        return creator ? creator() : nullptr;
    }

    bool CommandRegistry::Contains(const std::string& commandName) const
    {
        std::shared_lock lock{m_mutex};
        return m_creators.contains(commandName);
    }
}

#pragma once

#include "ICommand.h"
#include "../Diagnostics.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace artest
{
    class CommandRegistry
    {
    public:
        using Creator = std::function<std::unique_ptr<ICommand>()>;

        [[nodiscard]] OperationResult Register(std::string commandName, Creator creator);
        [[nodiscard]] std::unique_ptr<ICommand> Create(const std::string& commandName) const;
        [[nodiscard]] bool Contains(const std::string& commandName) const;

    private:
        mutable std::shared_mutex m_mutex;
        std::unordered_map<std::string, Creator> m_creators;
    };
}

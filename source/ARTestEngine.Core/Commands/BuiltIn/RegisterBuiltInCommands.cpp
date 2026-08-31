#include "RegisterBuiltInCommands.h"

#include "IfCommand.h"
#include "PowerOffCommand.h"
#include "PowerOnCommand.h"
#include "SendCanMessageCommand.h"
#include "WaitCommand.h"

#include <iterator>

namespace artest
{
    OperationResult RegisterBuiltInCommands(CommandRegistry& registry)
    {
        OperationResult combined;

        const auto registerCommand = [&combined, &registry](
                                         const std::string& name,
                                         CommandRegistry::Creator creator)
        {
            auto result = registry.Register(name, std::move(creator));
            combined.diagnostics.insert(
                combined.diagnostics.end(),
                std::make_move_iterator(result.diagnostics.begin()),
                std::make_move_iterator(result.diagnostics.end()));
        };

        registerCommand(WaitCommandName, [] { return std::make_unique<WaitCommand>(); });
        registerCommand(PowerOnCommandName, [] { return std::make_unique<PowerOnCommand>(); });
        registerCommand(PowerOffCommandName, [] { return std::make_unique<PowerOffCommand>(); });
        registerCommand(SendCanMessageCommandName, [] { return std::make_unique<SendCanMessageCommand>(); });
        registerCommand(IfCommandName, [] { return std::make_unique<IfCommand>(); });
        return combined;
    }
}

#include "PowerOnCommand.h"
#include "PowerOffCommand.h"
#include "SendCanMessageCommand.h"
#include <ARTest/Extension.h>

namespace
{
artest::sdk::Extension DefineExtension()
{
    artest::sdk::Extension extension{"com.artest.extension.hardware-commands", "0.1.0"};
    extension.AddCommand<artest::extensions::PowerOnCommand>(
        {.id = "com.artest.command.power.turn-on", .name = "Power On"});
    extension.AddCommand<artest::extensions::PowerOffCommand>(
        {.id = "com.artest.command.power.turn-off", .name = "Power Off"});
    extension.AddCommand<artest::extensions::SendCanMessageCommand>(
        {.id = "com.artest.command.can.send", .name = "Send CAN Message"});
    return extension;
}
} // namespace

ARTEST_EXPORT_EXTENSION(DefineExtension)

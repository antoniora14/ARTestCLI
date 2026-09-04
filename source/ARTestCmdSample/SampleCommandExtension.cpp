#include "PowerCycleCommand.h"
#include <ARTest/Extension.h>

namespace
{
artest::sdk::Extension DefineExtension()
{
    artest::sdk::Extension extension{"com.artest.extension.sample-command", "0.1.0"};
    extension.AddCommand<artest::extensions::PowerCycleCommand>(
        {.id = "com.artest.command.sample.power-cycle", .name = "ARTest Sample Power Cycle"});
    return extension;
}
} // namespace

ARTEST_EXPORT_EXTENSION(DefineExtension)

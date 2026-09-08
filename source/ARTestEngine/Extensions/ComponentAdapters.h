#pragma once
#include "IExtensionRuntime.h"

namespace artest::extensions
{

std::unique_ptr<ICommand> MakeExtensionCommand(std::shared_ptr<IExtensionRuntime> runtime, const std::string &typeId);
std::unique_ptr<IInstrument> MakeExtensionInstrument(std::shared_ptr<IExtensionRuntime> runtime, const std::string &typeId);

} // namespace artest::extensions

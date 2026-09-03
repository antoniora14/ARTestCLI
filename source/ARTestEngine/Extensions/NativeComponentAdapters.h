#pragma once
#include "NativeExtensionRuntime.h"
namespace artest::extensions
{
std::unique_ptr<ICommand> MakeNativeCommand(std::shared_ptr<NativeExtensionRuntime> runtime,
                                            const std::string &typeId);
std::unique_ptr<IInstrument> MakeNativeInstrument(std::shared_ptr<NativeExtensionRuntime> runtime,
                                                  const std::string &typeId);

} // namespace artest::extensions

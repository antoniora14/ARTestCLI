#pragma once

// The single authoring entry point. Public classes remain local to your DLL.
#include "Definition.h"
#include "detail/NativeAdapter.h"

// Define exactly once, in the extension's .cpp entry point. DefineFunction returns
// metadata/factories only; it must not perform I/O or construct driver instances.
#if defined(ARTEST_EXTENSION_EXPORTS)
#define ARTEST_EXPORT_EXTENSION(DefineFunction)                                                    \
    extern "C" ARTEST_ABI_EXPORT ARTestStatus ARTEST_ABI_CALL ARTestExtension_Query(               \
        std::uint32_t major, std::uint32_t minor, ARTestExtensionApiV0 *output,                    \
        ARTestErrorBuffer *error)                                                                  \
    {                                                                                              \
        return ::artest::sdk::detail::NativeAdapter<DefineFunction>::Query(major, minor, output,   \
                                                                           error);                 \
    }
#else
#define ARTEST_EXPORT_EXTENSION(DefineFunction)                                                    \
    static_assert(                                                                                 \
        false,                                                                                     \
        "Import ARTestSDK.props or define ARTEST_EXTENSION_EXPORTS for the extension DLL target.")
#endif

#pragma once
#include <string>
#include <utility>

namespace artest::extensions
{
// One synchronous root invocation and its nested service calls. Not a device
// global or Python flag: a later cleanup/new session receives its own scope.
struct InvocationEffects
{
    bool indeterminate = false;
    std::string diagnostic;
    std::string operation;

    void Record(std::string message, std::string source)
    {
        if (indeterminate) return; // Keep the original lost-ack diagnostic.
        indeterminate = true;
        diagnostic = std::move(message);
        operation = std::move(source);
    }
};
}

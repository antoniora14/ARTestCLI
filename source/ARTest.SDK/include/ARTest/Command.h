#pragma once

#include "Context.h"
#include "Parameters.h"

namespace artest::sdk
{
class Command
{
  public:
    virtual ~Command() = default;
    // Runtime-only semantic validation. Offline schemas remain the compiler's authority.
    [[nodiscard]] virtual Result Validate(const Parameters &) const
    {
        return Result::Success();
    }
    [[nodiscard]] virtual Result Execute(const Parameters &parameters, Context &context) = 0;
};
} // namespace artest::sdk

#pragma once

#include <ARTest/Command.h>

namespace artest::extensions
{
class PowerOffCommand final : public sdk::Command
{
  public:
    sdk::Result Validate(const sdk::Parameters &parameters) const override;
    sdk::Result Execute(const sdk::Parameters &parameters, sdk::Context &context) override;
};
} // namespace artest::extensions

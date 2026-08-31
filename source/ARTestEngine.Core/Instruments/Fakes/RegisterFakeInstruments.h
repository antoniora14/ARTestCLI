#pragma once

#include "../InstrumentRegistry.h"

namespace artest
{
    [[nodiscard]] OperationResult RegisterFakeInstruments(InstrumentRegistry& registry);
}

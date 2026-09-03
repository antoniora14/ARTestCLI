#pragma once

#include "ARTestEngine.Core/Instruments/InstrumentRegistry.h"

namespace artest
{
    [[nodiscard]] OperationResult RegisterFakeInstruments(InstrumentRegistry& registry);
}

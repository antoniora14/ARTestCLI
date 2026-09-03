#pragma once

#include "ARTestEngine.Core/Instruments/IInstrument.h"

#include <cstdint>
#include <vector>

namespace artest
{
    class ICanDevice : public IInstrument
    {
    public:
        [[nodiscard]] virtual OperationResult SendMessage(
            int channel,
            std::uint32_t messageId,
            const std::vector<std::uint8_t>& data) = 0;
    };
}

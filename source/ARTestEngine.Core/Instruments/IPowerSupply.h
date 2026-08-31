#pragma once

#include "IInstrument.h"

namespace artest
{
    class IPowerSupply : public IInstrument
    {
    public:
        [[nodiscard]] virtual OperationResult TurnOn(int channel) = 0;
        [[nodiscard]] virtual OperationResult TurnOff(int channel) = 0;
        [[nodiscard]] virtual OperationResult SetVoltage(int channel, double voltage) = 0;
        [[nodiscard]] virtual OperationResult SetCurrent(int channel, double current) = 0;
    };
}

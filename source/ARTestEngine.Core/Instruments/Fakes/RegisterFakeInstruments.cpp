#include "RegisterFakeInstruments.h"

#include "FakeCanDevice.h"
#include "FakePowerSupply.h"

#include <iterator>

namespace artest
{
    OperationResult RegisterFakeInstruments(InstrumentRegistry& registry)
    {
        OperationResult combined;

        auto powerResult = registry.Register(
            FakePowerSupplyType,
            [](IEventSink& sink) { return std::make_unique<FakePowerSupply>(sink); });
        combined.diagnostics.insert(
            combined.diagnostics.end(),
            std::make_move_iterator(powerResult.diagnostics.begin()),
            std::make_move_iterator(powerResult.diagnostics.end()));

        auto canResult = registry.Register(
            FakeCanDeviceType,
            [](IEventSink& sink) { return std::make_unique<FakeCanDevice>(sink); });
        combined.diagnostics.insert(
            combined.diagnostics.end(),
            std::make_move_iterator(canResult.diagnostics.begin()),
            std::make_move_iterator(canResult.diagnostics.end()));
        return combined;
    }
}

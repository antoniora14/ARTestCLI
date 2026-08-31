#pragma once

#include "ARTestEngine.Core/Execution/IEventSink.h"

#include <vector>

class RecordingEventSink final : public artest::IEventSink
{
public:
    void Publish(const artest::EngineEvent& event) noexcept override
    {
        try
        {
            events.push_back(event);
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] std::size_t Count(artest::EngineEventKind kind) const
    {
        std::size_t count = 0;
        for (const auto& event : events)
        {
            if (event.kind == kind)
            {
                ++count;
            }
        }
        return count;
    }

    std::vector<artest::EngineEvent> events;
};

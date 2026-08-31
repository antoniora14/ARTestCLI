#pragma once

#include "../ARTestEngine.Core/Execution/IEventSink.h"

#include <iosfwd>

namespace artest::cli
{
    class ConsoleEventSink final : public IEventSink
    {
    public:
        ConsoleEventSink(std::ostream& output, std::ostream& error) noexcept;
        void Publish(const EngineEvent& event) noexcept override;

    private:
        std::ostream& m_output;
        std::ostream& m_error;
    };
}

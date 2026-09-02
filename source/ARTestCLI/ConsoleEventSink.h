#pragma once

#include <iosfwd>
#include <string_view>

namespace artest::cli
{
    class ConsoleEventSink final
    {
    public:
        ConsoleEventSink(std::ostream& output, std::ostream& error) noexcept;
        void Publish(std::string_view eventJson) noexcept;

    private:
        std::ostream& m_output;
        std::ostream& m_error;
    };
}

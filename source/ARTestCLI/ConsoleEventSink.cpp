#include "ConsoleEventSink.h"

#include "ARTestEngineApi.h"
#include "../ThirdParty/json.hpp"

#include <cctype>
#include <cstdint>
#include <ostream>
#include <string>

namespace artest::cli
{
    namespace
    {
        std::string StepStatusText(const nlohmann::json& event)
        {
            auto value = event.value("stepStatus", std::string{"error"});
            if (value == "timedOut") return "TIMED_OUT";
            for (auto& character : value)
                character = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(character)));
            return value;
        }
    }

    ConsoleEventSink::ConsoleEventSink(std::ostream& output, std::ostream& error) noexcept
        : m_output(output), m_error(error)
    {
    }

    void ConsoleEventSink::Publish(std::string_view eventJson) noexcept
    {
        try
        {
            const auto event = nlohmann::json::parse(eventJson);
            const auto kind = event.value(
                "kind", static_cast<std::uint32_t>(ARTEST_ENGINE_EVENT_DIAGNOSTIC));
            const auto source = event.value("source", std::string{});
            const auto message = event.value("message", std::string{});
            switch (kind)
            {
            case ARTEST_ENGINE_EVENT_DIAGNOSTIC:
                // Informational extension lifecycle messages are not errors.
                (event.value("severity", 1U) <= 1U ? m_output : m_error)
                    << '[' << source << "]: " << message << '\n';
                break;
            case ARTEST_ENGINE_EVENT_INSTRUMENT_OPERATION:
                m_output << message << '\n';
                break;
            case ARTEST_ENGINE_EVENT_RUN_STATE_CHANGED:
                m_output << "[State] " << message << '\n';
                break;
            case ARTEST_ENGINE_EVENT_STEP_STARTED:
                m_output << "|> Executing step " << event.value("stepId", 0U)
                         << ": " << source << '\n';
                break;
            case ARTEST_ENGINE_EVENT_STEP_ATTEMPT_STARTED:
                if (event.value("attempt", 1U) > 1U)
                {
                    m_output << "   Attempt " << event.value("attempt", 1U) << '\n';
                }
                break;
            case ARTEST_ENGINE_EVENT_STEP_RETRY_SCHEDULED:
                m_output << "   Retry scheduled after attempt " << event.value("attempt", 0U)
                         << " in " << event.value("durationMs", 0LL)
                         << " ms\n";
                break;
            case ARTEST_ENGINE_EVENT_STEP_COMPLETED:
                m_output << "|< Step " << event.value("stepId", 0U) << ' '
                         << StepStatusText(event)
                         << " | attempts=" << event.value("attempt", 0U)
                         << " durationMs=" << event.value("durationMs", 0LL);
                if (!message.empty())
                {
                    m_output << " | " << message;
                }
                m_output << '\n';
                break;
            case ARTEST_ENGINE_EVENT_RUN_COMPLETED:
                m_output << "\nExecution finished with " << message << ".\n";
                break;
            case ARTEST_ENGINE_EVENT_INSTRUMENT_INITIALIZING:
            case ARTEST_ENGINE_EVENT_INSTRUMENT_INITIALIZED:
            case ARTEST_ENGINE_EVENT_INSTRUMENT_SHUTDOWN:
                break;
            }
        }
        catch (...)
        {
            try
            {
                m_error << "[ENGINE_EVENT_INVALID]: The Engine emitted an invalid event payload.\n";
            }
            catch (...) {}
        }
    }
}

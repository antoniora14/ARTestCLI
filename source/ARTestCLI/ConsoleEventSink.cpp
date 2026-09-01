#include "ConsoleEventSink.h"

#include <ostream>

namespace artest::cli
{
    namespace
    {
        const char* StepStatusText(StepStatus status)
        {
            switch (status)
            {
            case StepStatus::Passed: return "PASSED";
            case StepStatus::Failed: return "FAILED";
            case StepStatus::Error: return "ERROR";
            case StepStatus::Skipped: return "SKIPPED";
            case StepStatus::Cancelled: return "CANCELLED";
            case StepStatus::TimedOut: return "TIMED_OUT";
            }
            return "UNKNOWN";
        }
    }

    ConsoleEventSink::ConsoleEventSink(std::ostream& output, std::ostream& error) noexcept
        : m_output(output), m_error(error)
    {
    }

    void ConsoleEventSink::Publish(const EngineEvent& event) noexcept
    {
        try
        {
            switch (event.kind)
            {
            case EngineEventKind::Diagnostic:
                m_error << "[" << event.source << "]: " << event.message << '\n';
                break;
            case EngineEventKind::InstrumentOperation:
                m_output << event.message << '\n';
                break;
            case EngineEventKind::RunStateChanged:
                m_output << "[State] " << event.message << '\n';
                break;
            case EngineEventKind::StepStarted:
                m_output << "|> Executing step " << event.stepId.value_or(0)
                         << ": " << event.source << '\n';
                break;
            case EngineEventKind::StepAttemptStarted:
                if (event.attempt.value_or(1) > 1)
                {
                    m_output << "   Attempt " << event.attempt.value() << '\n';
                }
                break;
            case EngineEventKind::StepRetryScheduled:
                m_output << "   Retry scheduled after attempt " << event.attempt.value_or(0)
                         << " in " << event.duration.value_or(std::chrono::milliseconds{0}).count()
                         << " ms\n";
                break;
            case EngineEventKind::StepCompleted:
                m_output << "|< Step " << event.stepId.value_or(0) << ' '
                         << StepStatusText(event.stepStatus.value_or(StepStatus::Error))
                         << " | attempts=" << event.attempt.value_or(0)
                         << " durationMs="
                         << event.duration.value_or(std::chrono::milliseconds{0}).count();
                if (!event.message.empty())
                {
                    m_output << " | " << event.message;
                }
                m_output << '\n';
                break;
            case EngineEventKind::RunCompleted:
                m_output << "\nExecution finished with " << event.message << ".\n";
                break;
            case EngineEventKind::InstrumentInitializing:
            case EngineEventKind::InstrumentInitialized:
            case EngineEventKind::InstrumentShutdown:
                break;
            }
        }
        catch (...)
        {
        }
    }
}

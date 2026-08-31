#include "ConsoleEventSink.h"

#include <ostream>

namespace artest::cli
{
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
            case EngineEventKind::StepStarted:
                m_output << "|> Executing step " << event.stepId.value_or(0)
                         << ": " << event.source << '\n';
                break;
            case EngineEventKind::StepCompleted:
                if (event.stepStatus.has_value() && *event.stepStatus != StepStatus::Passed)
                {
                    m_error << "Step " << event.stepId.value_or(0)
                            << " failed: " << event.message << '\n';
                }
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

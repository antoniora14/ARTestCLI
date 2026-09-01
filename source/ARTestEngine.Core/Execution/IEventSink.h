#pragma once

#include "ExecutionResult.h"

#include <cstdint>
#include <optional>
#include <string>

namespace artest
{
    enum class EngineEventKind
    {
        Diagnostic,
        InstrumentInitializing,
        InstrumentInitialized,
        InstrumentShutdown,
        InstrumentOperation,
        RunStateChanged,
        StepStarted,
        StepAttemptStarted,
        StepRetryScheduled,
        StepCompleted,
        RunCompleted
    };

    enum class EngineEventSeverity
    {
        Trace,
        Information,
        Warning,
        Error
    };

    struct EngineEvent
    {
        EngineEventKind kind = EngineEventKind::Diagnostic;
        EngineEventSeverity severity = EngineEventSeverity::Information;
        std::string source;
        std::string message;
        std::optional<std::uint64_t> stepId;
        std::optional<StepStatus> stepStatus;
        std::optional<RunStatus> runStatus;
        std::optional<std::size_t> attempt;
        std::optional<std::chrono::milliseconds> duration;
    };

    class IEventSink
    {
    public:
        virtual ~IEventSink() = default;
        virtual void Publish(const EngineEvent& event) noexcept = 0;
    };

    class NullEventSink final : public IEventSink
    {
    public:
        void Publish(const EngineEvent&) noexcept override
        {
        }
    };
}

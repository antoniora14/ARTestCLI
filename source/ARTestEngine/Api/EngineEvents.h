#pragma once
#include "../../ARTestEngine.Core/Execution/IEventSink.h"
#include "../../ARTestEngine.Core/Execution/IExecutionControl.h"
#include "EngineMarshalling.h"
#include <mutex>
#include <unordered_map>
namespace artest::engine
{
class EventHub final : public artest::IEventSink
{
  public:
    std::uint64_t Subscribe(ARTestEngineEventFn callback, void *context)
    {
        std::scoped_lock lock{m_mutex};
        const auto id = m_nextId++;
        m_subscribers.emplace(id, Subscriber{callback, context});
        return id;
    }
    void Unsubscribe(std::uint64_t id) noexcept
    {
        std::scoped_lock lock{m_mutex};
        m_subscribers.erase(id);
    }
    void Publish(const artest::EngineEvent &event) noexcept override
    {
        try
        {
            nlohmann::json json{{"schema", "artest.schema.engine-event.v1"},
                                {"kind", static_cast<int>(event.kind)},
                                {"severity", static_cast<int>(event.severity)},
                                {"source", event.source},
                                {"message", event.message}};
            if (event.stepId)
                json["stepId"] = *event.stepId;
            if (event.attempt)
                json["attempt"] = *event.attempt;
            if (event.duration)
                json["durationMs"] = event.duration->count();
            if (event.stepStatus)
                json["stepStatus"] = StepStatusText(*event.stepStatus);
            if (event.runStatus)
                json["runStatus"] = RunStatusText(*event.runStatus);
            const auto text = json.dump();
            const auto payload = JsonPayload(text);
            std::vector<Subscriber> subscribers;
            {
                std::scoped_lock lock{m_mutex};
                for (const auto &[id, subscriber] : m_subscribers)
                {
                    static_cast<void>(id);
                    subscribers.push_back(subscriber);
                }
            }
            for (const auto &subscriber : subscribers)
                subscriber.callback(subscriber.context, &payload);
        }
        catch (...)
        {
        }
    }

  private:
    struct Subscriber
    {
        ARTestEngineEventFn callback;
        void *context;
    };
    std::mutex m_mutex;
    std::unordered_map<std::uint64_t, Subscriber> m_subscribers;
    std::uint64_t m_nextId = 1U;
};

class HostExecutionControl final : public artest::IExecutionControl
{
  public:
    HostExecutionControl(const ARTestSessionOptionsV0 &options, EventHub &events) noexcept
        : m_options(options), m_events(events)
    {
    }

    [[nodiscard]] artest::ExecutionDecision BeforeStep(
        const artest::StepExecutionInfo &step) override
    {
        if (m_options.before_step == nullptr)
            return artest::ExecutionDecision::Continue;

        ARTestStepExecutionInfoV0 info{sizeof(ARTestStepExecutionInfoV0), 0U,
                                       static_cast<std::uint64_t>(step.commandIndex), step.stepId,
                                       View(step.commandName)};
        ARTestExecutionDecision decision = ARTEST_EXECUTION_CONTINUE;
        char errorText[1024]{};
        ARTestErrorBuffer error{sizeof(ARTestErrorBuffer), 0U, errorText, sizeof(errorText), 0U};
        const auto status =
            m_options.before_step(m_options.control_context, &info, &decision, &error);
        if (status == ARTEST_STATUS_OK &&
            (decision == ARTEST_EXECUTION_CONTINUE || decision == ARTEST_EXECUTION_CANCEL))
        {
            return decision == ARTEST_EXECUTION_CONTINUE ? artest::ExecutionDecision::Continue
                                                         : artest::ExecutionDecision::Cancel;
        }

        const std::string message =
            status == ARTEST_STATUS_OK
                ? "The host execution-control callback returned an invalid decision."
            : errorText[0] == '\0' ? "The host execution-control callback failed."
                                   : std::string{errorText};
        m_events.Publish({artest::EngineEventKind::Diagnostic, artest::EngineEventSeverity::Error,
                          "execution-control", message, step.stepId});
        return artest::ExecutionDecision::Cancel;
    }

  private:
    ARTestSessionOptionsV0 m_options{};
    EventHub &m_events;
};

} // namespace artest::engine

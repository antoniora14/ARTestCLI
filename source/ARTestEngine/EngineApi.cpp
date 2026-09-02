#include "../ARTest.SDK/include/ARTestEngineApi.h"
#include "Extensions/NativeExtensionRuntime.h"

#include "../ARTestEngine.Core/Commands/BuiltIn/RegisterBuiltInCommands.h"
#include "../ARTestEngine.Core/Compilation/TestPlanCompiler.h"
#include "../ARTestEngine.Core/Execution/ExecutionSession.h"
#include "../ARTestEngine.Core/Instruments/Fakes/RegisterFakeInstruments.h"
#include "../ARTestEngine.Core/Instruments/InstrumentManager.h"
#include "../ARTestEngine.Core/Parsing/JsonTestPlanParser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] std::string ToString(ARTestStringView value)
    {
        return value.data == nullptr ? std::string{} : std::string{value.data, value.size};
    }

    [[nodiscard]] std::string PayloadText(const ARTestPayloadView* payload)
    {
        if (payload == nullptr
            || payload->struct_size < sizeof(ARTestPayloadView)
            || payload->encoding != ARTEST_PAYLOAD_ENCODING_JSON_UTF8
            || (payload->bytes.data == nullptr && payload->bytes.size != 0U))
        {
            throw std::invalid_argument("A valid JSON UTF-8 payload is required.");
        }
        return {
            reinterpret_cast<const char*>(payload->bytes.data),
            payload->bytes.size};
    }

    [[nodiscard]] ARTestStringView View(const std::string& value) noexcept
    {
        return {value.data(), value.size()};
    }

    [[nodiscard]] ARTestPayloadView JsonPayload(const std::string& value) noexcept
    {
        static const std::string schema = "artest.schema.generic-json.v1";
        static const std::string media = "application/json; charset=utf-8";
        return {
            sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema), View(media),
            {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}};
    }

    void SetError(ARTestErrorBuffer* error, const std::string& message) noexcept
    {
        if (error == nullptr) return;
        error->required_size = message.size() + 1U;
        if (error->data == nullptr || error->capacity == 0U) return;
        const auto count = (std::min)(message.size(), error->capacity - 1U);
        std::copy_n(message.data(), count, error->data);
        error->data[count] = '\0';
    }

    [[nodiscard]] std::string DiagnosticsText(
        const std::vector<artest::Diagnostic>& diagnostics)
    {
        if (diagnostics.empty()) return "The operation failed.";
        std::string text = diagnostics.front().code + ": "
            + diagnostics.front().message;
        if (!diagnostics.front().location.empty())
            text += " (" + diagnostics.front().location + ")";
        return text;
    }

    [[nodiscard]] const char* StepStatusText(artest::StepStatus value) noexcept
    {
        switch (value)
        {
        case artest::StepStatus::Passed: return "passed";
        case artest::StepStatus::Failed: return "failed";
        case artest::StepStatus::Error: return "error";
        case artest::StepStatus::Skipped: return "skipped";
        case artest::StepStatus::Cancelled: return "cancelled";
        case artest::StepStatus::TimedOut: return "timedOut";
        }
        return "error";
    }

    [[nodiscard]] const char* RunStatusText(artest::RunStatus value) noexcept
    {
        switch (value)
        {
        case artest::RunStatus::Passed: return "passed";
        case artest::RunStatus::Failed: return "failed";
        case artest::RunStatus::Error: return "error";
        case artest::RunStatus::Cancelled: return "cancelled";
        case artest::RunStatus::TimedOut: return "timedOut";
        }
        return "error";
    }

    [[nodiscard]] nlohmann::json SerializeDiagnostic(const artest::Diagnostic& value)
    {
        return {
            {"severity", value.severity == artest::DiagnosticSeverity::Error
                ? "error" : value.severity == artest::DiagnosticSeverity::Warning
                    ? "warning" : "information"},
            {"code", value.code},
            {"message", value.message},
            {"location", value.location}};
    }

    [[nodiscard]] nlohmann::json SerializeCompileResult(
        bool valid,
        std::size_t instrumentCount,
        std::size_t stepCount,
        const std::vector<artest::Diagnostic>& diagnostics)
    {
        nlohmann::json result{
            {"schema", "artest.schema.compile-result.v1"},
            {"valid", valid},
            {"summary", {
                {"instrumentDefinitions", instrumentCount},
                {"steps", stepCount}}},
            {"diagnostics", nlohmann::json::array()}};
        for (const auto& diagnostic : diagnostics)
            result["diagnostics"].push_back(SerializeDiagnostic(diagnostic));
        return result;
    }

    [[nodiscard]] nlohmann::json SerializeResult(const artest::RunResult& value)
    {
        nlohmann::json result{
            {"schema", "artest.schema.run-result.v1"},
            {"status", RunStatusText(value.status)},
            {"failureKind", static_cast<int>(value.failureKind)},
            {"summary", {
                {"plannedSteps", value.summary.plannedSteps},
                {"executedSteps", value.summary.executedSteps},
                {"passedSteps", value.summary.passedSteps},
                {"failedSteps", value.summary.failedSteps},
                {"errorSteps", value.summary.errorSteps},
                {"cancelledSteps", value.summary.cancelledSteps},
                {"timedOutSteps", value.summary.timedOutSteps},
                {"skippedSteps", value.summary.skippedSteps},
                {"totalAttempts", value.summary.totalAttempts},
                {"durationMs", value.summary.duration.count()}}},
            {"diagnostics", nlohmann::json::array()},
            {"steps", nlohmann::json::array()}};
        for (const auto& diagnostic : value.diagnostics)
            result["diagnostics"].push_back(SerializeDiagnostic(diagnostic));
        for (const auto& step : value.steps)
        {
            nlohmann::json item{
                {"stepId", step.stepId},
                {"command", step.commandName},
                {"status", StepStatusText(step.result.status)},
                {"message", step.result.message},
                {"durationMs", step.duration.count()},
                {"attempts", nlohmann::json::array()}};
            for (const auto& attempt : step.attempts)
                item["attempts"].push_back({
                    {"attempt", attempt.attempt},
                    {"status", StepStatusText(attempt.result.status)},
                    {"message", attempt.result.message},
                    {"durationMs", attempt.duration.count()}});
            result["steps"].push_back(std::move(item));
        }
        return result;
    }

    [[nodiscard]] ARTestStatus WriteJson(
        const nlohmann::json& value,
        const ARTestResultSinkV0* sink,
        ARTestErrorBuffer* error)
    {
        if (sink == nullptr || sink->struct_size < sizeof(ARTestResultSinkV0)
            || sink->write == nullptr)
        {
            SetError(error, "A valid result sink is required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        const auto text = value.dump();
        const auto payload = JsonPayload(text);
        return sink->write(sink->sink_context, &payload, error);
    }

    class EventHub final : public artest::IEventSink
    {
    public:
        std::uint64_t Subscribe(ARTestEngineEventFn callback, void* context)
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
        void Publish(const artest::EngineEvent& event) noexcept override
        {
            try
            {
                nlohmann::json json{
                    {"schema", "artest.schema.engine-event.v1"},
                    {"kind", static_cast<int>(event.kind)},
                    {"severity", static_cast<int>(event.severity)},
                    {"source", event.source},
                    {"message", event.message}};
                if (event.stepId) json["stepId"] = *event.stepId;
                if (event.attempt) json["attempt"] = *event.attempt;
                if (event.duration) json["durationMs"] = event.duration->count();
                if (event.stepStatus) json["stepStatus"] = StepStatusText(*event.stepStatus);
                if (event.runStatus) json["runStatus"] = RunStatusText(*event.runStatus);
                const auto text = json.dump();
                const auto payload = JsonPayload(text);
                std::vector<Subscriber> subscribers;
                {
                    std::scoped_lock lock{m_mutex};
                    for (const auto& [id, subscriber] : m_subscribers)
                    {
                        static_cast<void>(id);
                        subscribers.push_back(subscriber);
                    }
                }
                for (const auto& subscriber : subscribers)
                    subscriber.callback(subscriber.context, &payload);
            }
            catch (...) {}
        }
    private:
        struct Subscriber { ARTestEngineEventFn callback; void* context; };
        std::mutex m_mutex;
        std::unordered_map<std::uint64_t, Subscriber> m_subscribers;
        std::uint64_t m_nextId = 1U;
    };

    class HostExecutionControl final : public artest::IExecutionControl
    {
    public:
        HostExecutionControl(
            const ARTestSessionOptionsV0& options,
            EventHub& events) noexcept
            : m_options(options), m_events(events)
        {
        }

        [[nodiscard]] artest::ExecutionDecision BeforeStep(
            const artest::StepExecutionInfo& step) override
        {
            if (m_options.before_step == nullptr)
                return artest::ExecutionDecision::Continue;

            ARTestStepExecutionInfoV0 info{
                sizeof(ARTestStepExecutionInfoV0),
                0U,
                static_cast<std::uint64_t>(step.commandIndex),
                step.stepId,
                View(step.commandName)};
            ARTestExecutionDecision decision = ARTEST_EXECUTION_CONTINUE;
            char errorText[1024]{};
            ARTestErrorBuffer error{
                sizeof(ARTestErrorBuffer), 0U,
                errorText, sizeof(errorText), 0U};
            const auto status = m_options.before_step(
                m_options.control_context, &info, &decision, &error);
            if (status == ARTEST_STATUS_OK
                && (decision == ARTEST_EXECUTION_CONTINUE
                    || decision == ARTEST_EXECUTION_CANCEL))
            {
                return decision == ARTEST_EXECUTION_CONTINUE
                    ? artest::ExecutionDecision::Continue
                    : artest::ExecutionDecision::Cancel;
            }

            const std::string message = status == ARTEST_STATUS_OK
                ? "The host execution-control callback returned an invalid decision."
                : errorText[0] == '\0'
                    ? "The host execution-control callback failed."
                    : std::string{errorText};
            m_events.Publish({
                artest::EngineEventKind::Diagnostic,
                artest::EngineEventSeverity::Error,
                "execution-control",
                message,
                step.stepId});
            return artest::ExecutionDecision::Cancel;
        }

    private:
        ARTestSessionOptionsV0 m_options{};
        EventHub& m_events;
    };

    struct EngineContext
    {
        EngineContext()
            : runtime(std::make_shared<artest::extensions::NativeExtensionRuntime>(events)) {}
        [[nodiscard]] artest::OperationResult Initialize()
        {
            auto result = artest::RegisterBuiltInCommands(commands);
            if (!result.Succeeded()) return result;
            return artest::RegisterFakeInstruments(instruments);
        }
        EventHub events;
        std::shared_ptr<artest::extensions::NativeExtensionRuntime> runtime;
        artest::CommandRegistry commands;
        artest::InstrumentRegistry instruments;
    };
}

struct ARTestEngineOpaque { std::unique_ptr<EngineContext> value; };
struct ARTestCompiledPlanOpaque
{
    EngineContext* owner = nullptr;
    artest::TestPlan plan;
};
struct ARTestSubscriptionOpaque
{
    EngineContext* owner = nullptr;
    std::uint64_t id = 0U;
};
struct ARTestSessionOpaque
{
    EngineContext* owner = nullptr;
    std::unique_ptr<artest::InstrumentManager> instruments;
    std::unique_ptr<artest::IExecutionControl> control;
    std::unique_ptr<artest::ExecutionSession> execution;
    std::optional<artest::RunResult> result;
    std::mutex mutex;
};
struct ARTestResultOpaque { artest::RunResult value; };

namespace
{
    struct CompileAttempt
    {
        std::unique_ptr<ARTestCompiledPlanOpaque> plan;
        std::vector<artest::Diagnostic> diagnostics;
        std::size_t instrumentCount = 0U;
        std::size_t stepCount = 0U;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return plan != nullptr && !artest::ContainsErrors(diagnostics);
        }
    };

    void AppendDiagnostics(
        std::vector<artest::Diagnostic>& destination,
        const std::vector<artest::Diagnostic>& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    [[nodiscard]] CompileAttempt CompileForEngine(
        EngineContext& engine,
        const ARTestPayloadView* payload)
    {
        CompileAttempt attempt;
        artest::JsonTestPlanParser parser;
        auto parsed = parser.ParseText(PayloadText(payload), "engine-api");
        AppendDiagnostics(attempt.diagnostics, parsed.diagnostics);
        if (!parsed.Succeeded()) return attempt;

        attempt.instrumentCount = parsed.value->instruments.size();
        attempt.stepCount = parsed.value->steps.size();
        artest::InstrumentManager instruments{engine.instruments, engine.events};
        auto definitions = instruments.LoadDefinitions(parsed.value->instruments);
        AppendDiagnostics(attempt.diagnostics, definitions.diagnostics);
        if (!definitions.Succeeded()) return attempt;

        artest::TestPlanCompiler compiler{engine.commands, instruments};
        auto compiled = compiler.Compile(*parsed.value);
        AppendDiagnostics(attempt.diagnostics, compiled.diagnostics);
        if (!compiled.Succeeded()) return attempt;

        attempt.plan = std::make_unique<ARTestCompiledPlanOpaque>();
        attempt.plan->owner = &engine;
        attempt.plan->plan = std::move(*parsed.value);
        return attempt;
    }

    ARTestStatus ARTEST_ABI_CALL CreateEngine(
        const ARTestPayloadView* configuration,
        ARTestEngineHandle* output,
        ARTestErrorBuffer* error)
    {
        if (output == nullptr)
        {
            SetError(error, "An engine output pointer is required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            if (configuration != nullptr)
                static_cast<void>(nlohmann::json::parse(PayloadText(configuration)));
            auto engine = std::make_unique<ARTestEngineOpaque>();
            engine->value = std::make_unique<EngineContext>();
            const auto initialized = engine->value->Initialize();
            if (!initialized.Succeeded())
            {
                SetError(error, DiagnosticsText(initialized.diagnostics));
                return ARTEST_STATUS_INTERNAL_FAILURE;
            }
            *output = engine.release();
            return ARTEST_STATUS_OK;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while creating ARTestEngine.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    void ARTEST_ABI_CALL DestroyEngine(ARTestEngineHandle engine)
    {
        delete engine;
    }

    ARTestStatus ARTEST_ABI_CALL RefreshCatalog(
        ARTestEngineHandle engine,
        ARTestStringView approvedRoot,
        ARTestErrorBuffer* error)
    {
        if (engine == nullptr)
        {
            SetError(error, "A valid engine handle is required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            auto result = engine->value->runtime->Refresh(
                std::filesystem::path{ToString(approvedRoot)});
            if (result.Succeeded())
                result = engine->value->runtime->RegisterComponents(
                    engine->value->commands, engine->value->instruments);
            if (!result.Succeeded())
            {
                SetError(error, DiagnosticsText(result.diagnostics));
                return ARTEST_STATUS_EXTENSION_FAILURE;
            }
            return ARTEST_STATUS_OK;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while refreshing the extension catalog.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    ARTestStatus ARTEST_ABI_CALL GetCatalogSnapshot(
        ARTestEngineHandle engine,
        const ARTestResultSinkV0* sink,
        ARTestErrorBuffer* error)
    {
        if (engine == nullptr)
        {
            SetError(error, "A valid engine handle is required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            return WriteJson(engine->value->runtime->CatalogSnapshot(), sink, error);
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while serializing the catalog.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    ARTestStatus ARTEST_ABI_CALL CompilePlan(
        ARTestEngineHandle engine,
        const ARTestPayloadView* payload,
        ARTestCompiledPlanHandle* output,
        ARTestErrorBuffer* error)
    {
        if (engine == nullptr || output == nullptr)
        {
            SetError(error, "Engine and compiled-plan output handles are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            *output = nullptr;
            auto attempt = CompileForEngine(*engine->value, payload);
            if (!attempt.Succeeded())
            {
                SetError(error, DiagnosticsText(attempt.diagnostics));
                return ARTEST_STATUS_INVALID_ARGUMENT;
            }
            *output = attempt.plan.release();
            return ARTEST_STATUS_OK;
        }
        catch (const std::invalid_argument& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while compiling the plan.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    ARTestStatus ARTEST_ABI_CALL CompilePlanDetailed(
        ARTestEngineHandle engine,
        const ARTestPayloadView* payload,
        ARTestCompiledPlanHandle* output,
        const ARTestResultSinkV0* reportSink,
        ARTestErrorBuffer* error)
    {
        if (engine == nullptr || output == nullptr)
        {
            SetError(error, "Engine and compiled-plan output handles are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            *output = nullptr;
            auto attempt = CompileForEngine(*engine->value, payload);
            const auto report = SerializeCompileResult(
                attempt.Succeeded(),
                attempt.instrumentCount,
                attempt.stepCount,
                attempt.diagnostics);
            const auto writeStatus = WriteJson(report, reportSink, error);
            if (writeStatus != ARTEST_STATUS_OK) return writeStatus;
            if (attempt.Succeeded()) *output = attempt.plan.release();
            return ARTEST_STATUS_OK;
        }
        catch (const std::invalid_argument& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while compiling the plan.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    void ARTEST_ABI_CALL DestroyCompiledPlan(ARTestCompiledPlanHandle plan)
    {
        delete plan;
    }

    ARTestStatus ARTEST_ABI_CALL SubscribeEvents(
        ARTestEngineHandle engine,
        ARTestEngineEventFn callback,
        void* context,
        ARTestSubscriptionHandle* output,
        ARTestErrorBuffer* error)
    {
        if (engine == nullptr || callback == nullptr || output == nullptr)
        {
            SetError(error, "Engine, callback, and subscription output are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            auto subscription = std::make_unique<ARTestSubscriptionOpaque>();
            subscription->owner = engine->value.get();
            subscription->id = engine->value->events.Subscribe(callback, context);
            *output = subscription.release();
            return ARTEST_STATUS_OK;
        }
        catch (...)
        {
            SetError(error, "The event subscription could not be created.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    void ARTEST_ABI_CALL UnsubscribeEvents(
        ARTestEngineHandle engine,
        ARTestSubscriptionHandle subscription)
    {
        if (engine == nullptr || subscription == nullptr) return;
        if (subscription->owner == engine->value.get())
            engine->value->events.Unsubscribe(subscription->id);
        delete subscription;
    }

    ARTestStatus StartSessionInternal(
        ARTestEngineHandle engine,
        ARTestCompiledPlanHandle plan,
        std::unique_ptr<artest::IExecutionControl> control,
        ARTestSessionHandle* output,
        ARTestErrorBuffer* error)
    {
        if (engine == nullptr || plan == nullptr || output == nullptr
            || plan->owner != engine->value.get() || control == nullptr)
        {
            SetError(error, "Engine, compiled plan, and execution control are invalid or unrelated.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            auto session = std::make_unique<ARTestSessionOpaque>();
            session->owner = engine->value.get();
            session->control = std::move(control);
            session->instruments = std::make_unique<artest::InstrumentManager>(
                engine->value->instruments, engine->value->events);
            auto definitions =
                session->instruments->LoadDefinitions(plan->plan.instruments);
            if (!definitions.Succeeded())
            {
                SetError(error, DiagnosticsText(definitions.diagnostics));
                return ARTEST_STATUS_INVALID_STATE;
            }
            artest::TestPlanCompiler compiler{
                engine->value->commands, *session->instruments};
            auto compiled = compiler.Compile(plan->plan);
            if (!compiled.Succeeded())
            {
                SetError(error, DiagnosticsText(compiled.diagnostics));
                return ARTEST_STATUS_INVALID_STATE;
            }
            session->execution = std::make_unique<artest::ExecutionSession>(
                std::move(*compiled.value), *session->instruments,
                engine->value->events, *session->control);
            auto started = session->execution->Start();
            if (!started.Succeeded())
            {
                SetError(error, DiagnosticsText(started.diagnostics));
                return ARTEST_STATUS_INTERNAL_FAILURE;
            }
            *output = session.release();
            return ARTEST_STATUS_OK;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while starting the session.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    ARTestStatus ARTEST_ABI_CALL StartSession(
        ARTestEngineHandle engine,
        ARTestCompiledPlanHandle plan,
        ARTestSessionHandle* output,
        ARTestErrorBuffer* error)
    {
        try
        {
            return StartSessionInternal(
                engine,
                plan,
                std::make_unique<artest::RunToCompletionControl>(),
                output,
                error);
        }
        catch (...)
        {
            SetError(error, "The run-to-completion control could not be created.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    ARTestStatus ARTEST_ABI_CALL StartSessionControlled(
        ARTestEngineHandle engine,
        ARTestCompiledPlanHandle plan,
        const ARTestSessionOptionsV0* options,
        ARTestSessionHandle* output,
        ARTestErrorBuffer* error)
    {
        if (engine == nullptr || options == nullptr
            || options->struct_size < sizeof(ARTestSessionOptionsV0)
            || options->reserved != 0U || options->before_step == nullptr)
        {
            SetError(error, "Complete API 0.2 session options and a before-step callback are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            return StartSessionInternal(
                engine,
                plan,
                std::make_unique<HostExecutionControl>(*options, engine->value->events),
                output,
                error);
        }
        catch (...)
        {
            SetError(error, "The host execution control could not be created.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    ARTestStatus ARTEST_ABI_CALL CancelSession(
        ARTestSessionHandle session, ARTestErrorBuffer* error)
    {
        if (session == nullptr || !session->execution)
        {
            SetError(error, "A valid session handle is required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        session->execution->Cancel();
        return ARTEST_STATUS_OK;
    }

    ARTestStatus ARTEST_ABI_CALL WaitSession(
        ARTestSessionHandle session,
        std::uint32_t timeoutMs,
        ARTestBool32* completed,
        ARTestErrorBuffer* error)
    {
        if (session == nullptr || !session->execution || completed == nullptr)
        {
            SetError(error, "A valid session and completion output are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            std::scoped_lock lock{session->mutex};
            if (session->result.has_value())
            {
                *completed = ARTEST_TRUE;
                return ARTEST_STATUS_OK;
            }
            const bool ready = timeoutMs == UINT32_MAX
                ? true
                : session->execution->WaitFor(
                    std::chrono::milliseconds{timeoutMs});
            if (!ready)
            {
                *completed = ARTEST_FALSE;
                return ARTEST_STATUS_OK;
            }
            session->result = session->execution->Wait();
            *completed = ARTEST_TRUE;
            return ARTEST_STATUS_OK;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while waiting for the session.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    ARTestStatus ARTEST_ABI_CALL GetSessionState(
        ARTestSessionHandle session,
        ARTestSessionState* state,
        ARTestErrorBuffer* error)
    {
        if (session == nullptr || !session->execution || state == nullptr)
        {
            SetError(error, "A valid session and state output are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        *state = static_cast<ARTestSessionState>(session->execution->State());
        return ARTEST_STATUS_OK;
    }

    ARTestStatus ARTEST_ABI_CALL GetSessionResult(
        ARTestSessionHandle session,
        ARTestResultHandle* output,
        ARTestErrorBuffer* error)
    {
        if (session == nullptr || output == nullptr)
        {
            SetError(error, "A valid session and result output are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            std::scoped_lock lock{session->mutex};
            if (!session->result.has_value())
            {
                SetError(error, "The session has not completed.");
                return ARTEST_STATUS_INVALID_STATE;
            }
            auto result = std::make_unique<ARTestResultOpaque>();
            result->value = *session->result;
            *output = result.release();
            return ARTEST_STATUS_OK;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while copying the session result.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    void ARTEST_ABI_CALL DestroySession(ARTestSessionHandle session)
    {
        delete session;
    }

    ARTestStatus ARTEST_ABI_CALL SerializeRunResult(
        ARTestResultHandle result,
        const ARTestResultSinkV0* sink,
        ARTestErrorBuffer* error)
    {
        if (result == nullptr)
        {
            SetError(error, "A valid result handle is required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            return WriteJson(SerializeResult(result->value), sink, error);
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown failure while serializing the run result.");
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
    }

    void ARTEST_ABI_CALL DestroyRunResult(ARTestResultHandle result)
    {
        delete result;
    }
}

extern "C" ARTEST_ENGINE_EXPORT ARTestStatus ARTEST_ABI_CALL
    ARTestEngine_QueryApi(
        std::uint32_t requestedMajor,
        std::uint32_t requestedMinor,
        ARTestEngineApiV0* api,
        ARTestErrorBuffer* error)
{
    if (api == nullptr)
    {
        SetError(error, "An ARTestEngineApiV0 output table is required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    if (requestedMajor != ARTEST_ENGINE_API_MAJOR
        || requestedMinor > ARTEST_ENGINE_API_MINOR)
    {
        SetError(error, "The requested ARTestEngine API version is incompatible.");
        return ARTEST_STATUS_INCOMPATIBLE_ABI;
    }
    const auto negotiatedSize = requestedMinor >= 2U
        ? static_cast<std::uint32_t>(sizeof(ARTestEngineApiV0))
        : ARTEST_ENGINE_API_V0_1_SIZE;
    if (api->struct_size < negotiatedSize)
    {
        SetError(error, "The Engine API output table is smaller than the requested minor version.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }

    ARTestEngineApiV0 table{
        negotiatedSize,
        ARTEST_ENGINE_API_MAJOR,
        requestedMinor,
        0U,
        &CreateEngine,
        &DestroyEngine,
        &RefreshCatalog,
        &GetCatalogSnapshot,
        &CompilePlan,
        &DestroyCompiledPlan,
        &SubscribeEvents,
        &UnsubscribeEvents,
        &StartSession,
        &CancelSession,
        &WaitSession,
        &GetSessionState,
        &GetSessionResult,
        &DestroySession,
        &SerializeRunResult,
        &DestroyRunResult,
        &CompilePlanDetailed,
        &StartSessionControlled};
    const auto callerSize = api->struct_size;
    const auto clearSize = (std::min)(
        static_cast<std::size_t>(callerSize), sizeof(ARTestEngineApiV0));
    std::memset(api, 0, clearSize);
    std::memcpy(api, &table, negotiatedSize);
    return ARTEST_STATUS_OK;
}

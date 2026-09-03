#include "EngineFunctions.h"
#include "EngineHandles.h"
#include "EngineMarshalling.h"
namespace artest::engine
{
ARTestStatus ARTEST_ABI_CALL SubscribeEvents(ARTestEngineHandle engine,
                                             ARTestEngineEventFn callback, void *context,
                                             ARTestSubscriptionHandle *output,
                                             ARTestErrorBuffer *error)
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

void ARTEST_ABI_CALL UnsubscribeEvents(ARTestEngineHandle engine,
                                       ARTestSubscriptionHandle subscription)
{
    if (engine == nullptr || subscription == nullptr)
        return;
    if (subscription->owner == engine->value.get())
        engine->value->events.Unsubscribe(subscription->id);
    delete subscription;
}

ARTestStatus StartSessionInternal(ARTestEngineHandle engine, ARTestCompiledPlanHandle plan,
                                  std::unique_ptr<artest::IExecutionControl> control,
                                  ARTestSessionHandle *output, ARTestErrorBuffer *error)
{
    if (engine == nullptr || plan == nullptr || output == nullptr ||
        plan->owner != engine->value.get() || control == nullptr)
    {
        SetError(error, "Engine, compiled plan, and execution control are invalid or unrelated.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        *output = nullptr;
        auto session = std::make_unique<ARTestSessionOpaque>();
        session->owner = engine->value.get();
        {
            std::scoped_lock lock{engine->value->mutex};
            if (plan->revision != engine->value->revision || !engine->value->runLease.expired())
            {
                SetError(error, "The plan is stale or another session handle still owns this "
                                "Engine. Destroy the prior session first.");
                return ARTEST_STATUS_INVALID_STATE;
            }
            session->runLease = std::make_shared<int>(0);
            engine->value->runLease = session->runLease;
        }
        session->control = std::move(control);
        session->instruments = std::make_unique<artest::InstrumentManager>(
            engine->value->instruments, engine->value->events);
        auto *context = engine->value.get();
        auto *manager = session->instruments.get();
        const auto definitions = plan->plan.instruments;
        session->execution = std::make_unique<artest::ExecutionSession>(
            plan->steps, engine->value->commands, *manager, engine->value->events,
            *session->control, [context, manager, definitions] {
                auto activated = context->Activate();
                return activated.Succeeded() ? manager->LoadDefinitions(definitions) : activated;
            });
        auto started = session->execution->Start();
        if (!started.Succeeded())
        {
            SetError(error, DiagnosticsText(started.diagnostics));
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        *output = session.release();
        return ARTEST_STATUS_OK;
    }
    catch (const std::exception &exception)
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

ARTestStatus ARTEST_ABI_CALL StartSession(ARTestEngineHandle engine, ARTestCompiledPlanHandle plan,
                                          ARTestSessionHandle *output, ARTestErrorBuffer *error)
{
    try
    {
        return StartSessionInternal(
            engine, plan, std::make_unique<artest::RunToCompletionControl>(), output, error);
    }
    catch (...)
    {
        SetError(error, "The run-to-completion control could not be created.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

ARTestStatus ARTEST_ABI_CALL StartSessionControlled(ARTestEngineHandle engine,
                                                    ARTestCompiledPlanHandle plan,
                                                    const ARTestSessionOptionsV0 *options,
                                                    ARTestSessionHandle *output,
                                                    ARTestErrorBuffer *error)
{
    if (engine == nullptr || options == nullptr ||
        options->struct_size < sizeof(ARTestSessionOptionsV0) || options->reserved != 0U ||
        options->before_step == nullptr)
    {
        SetError(error,
                 "Complete API 0.2 session options and a before-step callback are required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        return StartSessionInternal(
            engine, plan, std::make_unique<HostExecutionControl>(*options, engine->value->events),
            output, error);
    }
    catch (...)
    {
        SetError(error, "The host execution control could not be created.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

ARTestStatus ARTEST_ABI_CALL CancelSession(ARTestSessionHandle session, ARTestErrorBuffer *error)
{
    if (session == nullptr || !session->execution)
    {
        SetError(error, "A valid session handle is required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    session->execution->Cancel();
    return ARTEST_STATUS_OK;
}

ARTestStatus ARTEST_ABI_CALL WaitSession(ARTestSessionHandle session, std::uint32_t timeoutMs,
                                         ARTestBool32 *completed, ARTestErrorBuffer *error)
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
                               : session->execution->WaitFor(std::chrono::milliseconds{timeoutMs});
        if (!ready)
        {
            *completed = ARTEST_FALSE;
            return ARTEST_STATUS_OK;
        }
        session->result = session->execution->Wait();
        *completed = ARTEST_TRUE;
        return ARTEST_STATUS_OK;
    }
    catch (const std::exception &exception)
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

ARTestStatus ARTEST_ABI_CALL GetSessionState(ARTestSessionHandle session, ARTestSessionState *state,
                                             ARTestErrorBuffer *error)
{
    if (session == nullptr || !session->execution || state == nullptr)
    {
        SetError(error, "A valid session and state output are required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    *state = static_cast<ARTestSessionState>(session->execution->State());
    return ARTEST_STATUS_OK;
}

ARTestStatus ARTEST_ABI_CALL GetSessionResult(ARTestSessionHandle session,
                                              ARTestResultHandle *output, ARTestErrorBuffer *error)
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
    catch (const std::exception &exception)
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

ARTestStatus ARTEST_ABI_CALL SerializeRunResult(ARTestResultHandle result,
                                                const ARTestResultSinkV0 *sink,
                                                ARTestErrorBuffer *error)
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
    catch (const std::exception &exception)
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
} // namespace artest::engine

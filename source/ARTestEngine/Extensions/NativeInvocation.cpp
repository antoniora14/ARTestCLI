#include "ExtensionRuntimeState.h"
namespace artest::extensions
{
ValueResult<std::shared_ptr<ComponentLease>> ExtensionRuntime::CreateComponent(
    const std::string &typeId, const nlohmann::json &configuration)
{
    ValueResult<std::shared_ptr<ComponentLease>> result;
    if (m_implementation->python.Contains(typeId))
    {
        try { result.value = m_implementation->python.Create(typeId, configuration); }
        catch (const std::exception &error)
        { result.diagnostics.push_back({DiagnosticSeverity::Error, "PYTHON_COMPONENT_CREATE_FAILED", error.what(), typeId}); }
        return result;
    }
    std::pair<std::shared_ptr<NativeModule>, ComponentRecord> entry;
    {
        std::scoped_lock lock{m_implementation->catalogMutex};
        const auto found = m_implementation->types.find(typeId);
        if (found != m_implementation->types.end())
            entry = found->second;
    }
    if (!entry.first)
    {
        result.diagnostics.push_back({DiagnosticSeverity::Error, "EXTENSION_COMPONENT_UNKNOWN",
                                      "Unknown extension component type: " + typeId, typeId});
        return result;
    }
    const auto text = configuration.dump();
    const auto payload = JsonPayload(text);

    ARTestComponentHandle handle = nullptr;
    ErrorStorage error;
    ARTestStatus status;
    {
        std::scoped_lock lock{entry.first->invocationMutex};
        status = entry.first->api.create_component(entry.first->extension, View(typeId), &payload,
                                                   &handle, &error.buffer);
    }
    // Own even a handle returned alongside an extension error, and guard allocation failure.
    auto destroy = [&entry](ARTestComponentOpaque *value) {
        if (value)
        {
            std::scoped_lock lock{entry.first->invocationMutex};
            entry.first->api.destroy_component(entry.first->extension, value);
        }
    };
    std::unique_ptr<ARTestComponentOpaque, decltype(destroy)> pending{handle, destroy};

    if (status != ARTEST_STATUS_OK || handle == nullptr)
    {
        result.diagnostics.push_back(
            {DiagnosticSeverity::Error, "EXTENSION_COMPONENT_CREATE_FAILED",
             error.Message("The extension component could not be created."), typeId});
        return result;
    }

    result.value = std::make_shared<NativeComponentInstance>(entry.first, entry.second, handle);
    pending.release();
    return result;
}

OperationResult ExtensionRuntime::Invoke(
    const std::shared_ptr<ComponentLease> &lease, const std::string &operationId,
    const nlohmann::json &request, const CancellationToken *cancellation, InvocationOutput *response)
{
    if (!lease)
        return OperationResult::Failure("EXTENSION_COMPONENT_INVALID",
                                        "A valid extension component is required.");
    const auto text = request.dump();
    const auto payload = JsonPayload(text);
    struct Capture
    {
        std::string text;
        std::string schema;
        bool written = false;
    } capture;
    const auto write = [](void *context, const ARTestPayloadView *value,
                          ARTestErrorBuffer *) noexcept -> ARTestStatus {
        if (context == nullptr || value == nullptr ||
            value->encoding != ARTEST_PAYLOAD_ENCODING_JSON_UTF8)
            return ARTEST_STATUS_INVALID_ARGUMENT;
        try
        {
            if (value->struct_size < sizeof(ARTestPayloadView) ||
                (!value->bytes.data && value->bytes.size))
                return ARTEST_STATUS_INVALID_ARGUMENT;
            auto &capture = *static_cast<Capture *>(context);
            if (capture.written || value->bytes.size > 1024 * 1024)
                return ARTEST_STATUS_INVALID_ARGUMENT;
            capture.written = true;
            capture.schema = ToString(value->schema_id);
            capture.text.assign(
                reinterpret_cast<const char *>(value->bytes.data), value->bytes.size);
            return ARTEST_STATUS_OK;
        }
        catch (...)
        {
            return ARTEST_STATUS_HOST_FAILURE;
        }
    };
    ARTestResultSinkV0 sink{sizeof(ARTestResultSinkV0), 0U, &capture, write};
    const auto cancelled = [](void *context) noexcept -> ARTestBool32 {
        const auto *token = static_cast<const CancellationToken *>(context);
        return token != nullptr && token->IsCancellationRequested() ? ARTEST_TRUE : ARTEST_FALSE;
    };
    ARTestInvocationContextV0 invocation{sizeof(ARTestInvocationContextV0),
                                         0U,
                                         1U,
                                         0U,
                                         const_cast<CancellationToken *>(cancellation),
                                         cancelled};
    if (cancellation && cancellation->Deadline())
    {
        // Use the same steady-clock epoch as NativeServiceBroker::MonotonicTime.
        // Service calls forward this context, so commands and drivers share the
        // attempt deadline. Lifecycle cleanup has no token and stays unconditional.
        invocation.deadline_monotonic_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                cancellation->Deadline()->time_since_epoch()).count());
    }
    ErrorStorage error;
    ARTestStatus status;
    {
        status = InvokeAbi(lease, View(operationId), &payload, &invocation, &sink, &error.buffer);
    }
    if (m_implementation->python.Indeterminate())
        return OperationResult::Failure("EXTENSION_OUTCOME_INDETERMINATE",
            error.Message("A worker failed; hardware effects are unknown. No retry is permitted."), operationId);
    if (status != ARTEST_STATUS_OK)
        return OperationResult::Failure(
            status == ARTEST_STATUS_CANCELLED   ? "EXTENSION_CANCELLED"
            : status == ARTEST_STATUS_TIMED_OUT ? "EXTENSION_TIMED_OUT"
                                                : "EXTENSION_INVOCATION_FAILED",
            error.Message("The extension invocation failed."), operationId);
    if (response != nullptr && !capture.text.empty())
    {
        try
        {
            response->data = nlohmann::json::parse(capture.text);
            response->schemaId = capture.schema;
        }
        catch (const std::exception &exception)
        {
            return OperationResult::Failure("EXTENSION_RESULT_INVALID", exception.what(),
                                            operationId);
        }
    }
    return OperationResult::Success();
}

OperationResult ExtensionRuntime::RegisterService(
    std::string instanceId, const std::shared_ptr<ComponentLease> &lease)
{
    const auto component = std::dynamic_pointer_cast<NativeComponentInstance>(lease);
    const auto python = std::dynamic_pointer_cast<PythonComponent>(lease);
    if (instanceId.empty() || (!component && !python))
        return OperationResult::Failure("EXTENSION_SERVICE_INVALID",
                                        "Service instance ID and component are required.");
    std::scoped_lock lock{m_implementation->broker.serviceMutex};
    if (m_implementation->broker.services.contains(instanceId))
        return OperationResult::Failure("EXTENSION_SERVICE_DUPLICATE",
                                        "The service instance ID is already active.", instanceId);
    m_implementation->broker.services.emplace(std::move(instanceId),
        NativeServiceBroker::Endpoint{lease, component ? component->record.contractId : python->contract});
    return OperationResult::Success();
}

void ExtensionRuntime::UnregisterService(const std::string &instanceId) noexcept
{
    std::scoped_lock lock{m_implementation->broker.serviceMutex};
    m_implementation->broker.services.erase(instanceId);
}
ARTestStatus ExtensionRuntime::InvokeAbi(const std::shared_ptr<ComponentLease> &lease,
    ARTestStringView operation, const ARTestPayloadView *request,
    const ARTestInvocationContextV0 *invocation, const ARTestResultSinkV0 *sink, ARTestErrorBuffer *error)
{
    if (const auto native = std::dynamic_pointer_cast<NativeComponentInstance>(lease))
    {
        std::scoped_lock lock{native->module->invocationMutex};
        return native->module->api.invoke_component(native->module->extension, native->handle,
            operation, request, invocation, sink, error);
    }
    if (const auto python = std::dynamic_pointer_cast<PythonComponent>(lease))
        return m_implementation->python.Invoke(python, operation, request, invocation, sink, error);
    return ARTEST_STATUS_INVALID_ARGUMENT;
}
} // namespace artest::extensions

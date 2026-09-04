#include "NativeRuntimeState.h"
namespace artest::extensions
{
ValueResult<std::shared_ptr<NativeComponentInstance>> NativeExtensionRuntime::CreateComponent(
    const std::string &typeId, const nlohmann::json &configuration)
{
    ValueResult<std::shared_ptr<NativeComponentInstance>> result;
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

OperationResult NativeExtensionRuntime::Invoke(
    const std::shared_ptr<NativeComponentInstance> &component, const std::string &operationId,
    const nlohmann::json &request, const CancellationToken *cancellation, nlohmann::json *response)
{
    if (!component)
        return OperationResult::Failure("EXTENSION_COMPONENT_INVALID",
                                        "A valid extension component is required.");
    const auto text = request.dump();
    const auto payload = JsonPayload(text);
    struct Capture
    {
        std::string text;
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
            static_cast<Capture *>(context)->text.assign(
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
        std::scoped_lock lock{component->module->invocationMutex};
        status = component->module->api.invoke_component(
            component->module->extension, component->handle, View(operationId), &payload,
            &invocation, &sink, &error.buffer);
    }
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
            *response = nlohmann::json::parse(capture.text);
        }
        catch (const std::exception &exception)
        {
            return OperationResult::Failure("EXTENSION_RESULT_INVALID", exception.what(),
                                            operationId);
        }
    }
    return OperationResult::Success();
}

OperationResult NativeExtensionRuntime::RegisterService(
    std::string instanceId, const std::shared_ptr<NativeComponentInstance> &component)
{
    if (instanceId.empty() || !component)
        return OperationResult::Failure("EXTENSION_SERVICE_INVALID",
                                        "Service instance ID and component are required.");
    std::scoped_lock lock{m_implementation->broker.serviceMutex};
    if (m_implementation->broker.services.contains(instanceId))
        return OperationResult::Failure("EXTENSION_SERVICE_DUPLICATE",
                                        "The service instance ID is already active.", instanceId);
    m_implementation->broker.services.emplace(std::move(instanceId), component);
    return OperationResult::Success();
}

void NativeExtensionRuntime::UnregisterService(const std::string &instanceId) noexcept
{
    std::scoped_lock lock{m_implementation->broker.serviceMutex};
    m_implementation->broker.services.erase(instanceId);
}
} // namespace artest::extensions

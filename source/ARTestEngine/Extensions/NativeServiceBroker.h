#pragma once
#include "../../ARTestEngine.Core/Execution/IEventSink.h"
#include "NativeModule.h"
#include <chrono>
#include <map>
namespace artest::extensions
{
// Service handles retain a component/module lease; callbacks never hold the catalog lock.
class NativeServiceBroker
{
  public:
    explicit NativeServiceBroker(IEventSink &sink) noexcept : eventSink(sink)
    {
        hostApi = {sizeof(ARTestHostApiV0),
                   ARTEST_EXTENSION_ABI_MAJOR,
                   ARTEST_EXTENSION_ABI_MINOR,
                   0U,
                   this,
                   &Log,
                   &MonotonicTime,
                   &ResolveService,
                   &InvokeService,
                   &ReleaseService};
    }
    struct ServiceLease
    {
        std::shared_ptr<NativeComponentInstance> component;
    };
    static void ARTEST_ABI_CALL Log(void *context, ARTestLogSeverity severity,
                                    ARTestStringView category, ARTestStringView message) noexcept
    {
        auto &self = *static_cast<NativeServiceBroker *>(context);
        try
        {
            self.eventSink.Publish({EngineEventKind::Diagnostic,
                                    severity == ARTEST_LOG_ERROR ? EngineEventSeverity::Error
                                    : severity == ARTEST_LOG_WARNING
                                        ? EngineEventSeverity::Warning
                                        : EngineEventSeverity::Information,
                                    ToString(category), ToString(message)});
        }
        catch (...)
        {
        }
    }
    static std::uint64_t ARTEST_ABI_CALL MonotonicTime(void *) noexcept
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count());
    }
    static ARTestStatus ARTEST_ABI_CALL ResolveService(void *context, ARTestStringView contractId,
                                                       ARTestStringView instanceId,
                                                       ARTestServiceHandle *service,
                                                       ARTestErrorBuffer *error) noexcept
    {
        if (service == nullptr)
        {
            SetError(error, "A service output pointer is required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        auto &self = *static_cast<NativeServiceBroker *>(context);
        try
        {
            std::scoped_lock lock{self.serviceMutex};
            const auto found = self.services.find(ToString(instanceId));
            auto component = found == self.services.end() ? nullptr : found->second.lock();
            if (!component || component->record.contractId != ToString(contractId))
            {
                SetError(error, "The configured service instance was not found.");
                return ARTEST_STATUS_NOT_FOUND;
            }
            *service =
                reinterpret_cast<ARTestServiceHandle>(new ServiceLease{std::move(component)});
            return ARTEST_STATUS_OK;
        }
        catch (...)
        {
            SetError(error, "The host failed while resolving a service.");
            return ARTEST_STATUS_HOST_FAILURE;
        }
    }
    static ARTestStatus ARTEST_ABI_CALL InvokeService(void *, ARTestServiceHandle service,
                                                      ARTestStringView operation,
                                                      const ARTestPayloadView *request,
                                                      const ARTestInvocationContextV0 *invocation,
                                                      const ARTestResultSinkV0 *resultSink,
                                                      ARTestErrorBuffer *error) noexcept
    {
        if (service == nullptr)
            return ARTEST_STATUS_INVALID_ARGUMENT;
        auto &component = *reinterpret_cast<ServiceLease *>(service)->component;
        std::scoped_lock lock{component.module->invocationMutex};
        return component.module->api.invoke_component(component.module->extension, component.handle,
                                                      operation, request, invocation, resultSink,
                                                      error);
    }
    static void ARTEST_ABI_CALL ReleaseService(void *, ARTestServiceHandle service) noexcept
    {
        delete reinterpret_cast<ServiceLease *>(service);
    }
    IEventSink &eventSink;
    ARTestHostApiV0 hostApi{};
    std::map<std::string, std::weak_ptr<NativeComponentInstance>> services;
    mutable std::mutex serviceMutex;
};

} // namespace artest::extensions

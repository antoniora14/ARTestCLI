#pragma once
#include "../../ARTestEngine.Core/Execution/IEventSink.h"
#include "NativeModule.h"
#include <chrono>
#include <map>
#include <functional>
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
        std::shared_ptr<ComponentLease> component;
    };
    struct Endpoint
    {
        std::weak_ptr<ComponentLease> component;
        std::string contract;
    };
    using InvokeCallback = std::function<ARTestStatus(const std::shared_ptr<ComponentLease>&,
        ARTestStringView, const ARTestPayloadView*, const ARTestInvocationContextV0*,
        const ARTestResultSinkV0*, ARTestErrorBuffer*)>;
    InvokeCallback invoke;
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
            auto component = found == self.services.end() ? nullptr : found->second.component.lock();
            if (!component || found->second.contract != ToString(contractId))
            {
                SetError(error, "The configured service instance was not found.");
                return ARTEST_STATUS_NOT_FOUND;
            }
            const auto token = reinterpret_cast<ARTestServiceHandle>(self.nextLease++);
            self.leases.emplace(token, std::make_shared<ServiceLease>(ServiceLease{std::move(component)}));
            *service = token;
            return ARTEST_STATUS_OK;
        }
        catch (...)
        {
            SetError(error, "The host failed while resolving a service.");
            return ARTEST_STATUS_HOST_FAILURE;
        }
    }
    static ARTestStatus ARTEST_ABI_CALL InvokeService(void *context, ARTestServiceHandle service,
                                                      ARTestStringView operation,
                                                      const ARTestPayloadView *request,
                                                      const ARTestInvocationContextV0 *invocation,
                                                      const ARTestResultSinkV0 *resultSink,
                                                      ARTestErrorBuffer *error) noexcept
    {
        if (service == nullptr)
            return ARTEST_STATUS_INVALID_ARGUMENT;
        try
        {
            auto &self = *static_cast<NativeServiceBroker *>(context);
            std::shared_ptr<ServiceLease> lease;
            {
                std::scoped_lock lock{self.serviceMutex};
                const auto found = self.leases.find(service);
                if (found == self.leases.end()) return ARTEST_STATUS_NOT_FOUND;
                lease = found->second;
            }
            // The lease survives callbacks; never execute extension code under the broker lock.
            return self.invoke(lease->component, operation, request, invocation, resultSink, error);
        }
        catch (...) { SetError(error, "Service invocation failed."); return ARTEST_STATUS_HOST_FAILURE; }
    }
    static void ARTEST_ABI_CALL ReleaseService(void *context, ARTestServiceHandle service) noexcept
    {
        auto &self = *static_cast<NativeServiceBroker *>(context);
        std::shared_ptr<ServiceLease> released;
        {
            std::scoped_lock lock{self.serviceMutex};
            const auto found = self.leases.find(service);
            if (found != self.leases.end()) { released = std::move(found->second); self.leases.erase(found); }
        }
    }
    IEventSink &eventSink;
    ARTestHostApiV0 hostApi{};
    std::map<std::string, Endpoint> services;
    std::map<ARTestServiceHandle, std::shared_ptr<ServiceLease>> leases;
    std::uintptr_t nextLease = 1;
    mutable std::mutex serviceMutex;
};

} // namespace artest::extensions

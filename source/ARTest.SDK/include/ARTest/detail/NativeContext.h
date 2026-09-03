#pragma once

#include "../Context.h"
#include "Marshalling.h"
#include <thread>

namespace artest::sdk::detail
{
class NativeContext final : public Context
{
private:
    const ARTestHostApiV0& m_host;
    const ARTestInvocationContextV0& m_invocation;
    std::string_view m_category, m_instrument;
    bool m_cleanup;

  public:
    NativeContext(const ARTestHostApiV0 &host, const ARTestInvocationContextV0 &invocation,
                  std::string_view category, std::string_view instrument, bool cleanup = false)
        : m_host(host), m_invocation(invocation), m_category(category), m_instrument(instrument),
          m_cleanup(cleanup)
    {
    }

    std::string_view InstrumentId() const noexcept override
    {
        return m_instrument;
    }

    Result Checkpoint() const override
    {
        // Cancellation must not suppress cleanup after partial initialization or a failed step.
        if (m_cleanup)
            return Result::Success();
        try
        {
            if (m_invocation.deadline_monotonic_ns &&
                m_host.monotonic_time_ns(m_host.host_context) >= m_invocation.deadline_monotonic_ns)
                return Result::Failure(Status::TimedOut, "The invocation deadline expired.");
            if (m_invocation.is_cancellation_requested &&
                m_invocation.is_cancellation_requested(m_invocation.cancellation_context))
                return Result::Failure(Status::Cancelled, "The invocation was cancelled.");
            return Result::Success();
        }
        catch (...)
        {
            return Result::Failure(Status::HostFailure, "The host checkpoint callback threw.");
        }
    }

    Result WaitFor(std::chrono::milliseconds duration) override
    {
        const auto start = std::chrono::steady_clock::now();
        if (duration.count() < 0 ||
            duration > std::chrono::duration_cast<std::chrono::milliseconds>(
                           (std::chrono::steady_clock::time_point::max)() - start))
            return Result::Failure(Status::InvalidArgument,
                                   "Wait duration is negative or out of range.");
        const auto end = start + duration;
        for (;;)
        {
            if (auto status = Checkpoint(); !status)
                return status;
            const auto now = std::chrono::steady_clock::now();
            if (now >= end)
                return Result::Success();
            std::this_thread::sleep_until((std::min)(end, now + std::chrono::milliseconds{5}));
        }
    }

    void Log(LogLevel level, std::string_view message) override
    {
        m_host.log(m_host.host_context, static_cast<ARTestLogSeverity>(level), View(m_category), View(message));
    }

    Result Call(std::string_view contract, std::string_view instance, std::string_view operation, const Json &request) override
    {
        if (contract.empty() || instance.empty() || operation.empty() || !request.is_object() ||
            contract.find('\0') != std::string_view::npos ||
            instance.find('\0') != std::string_view::npos ||
            operation.find('\0') != std::string_view::npos)
            return Result::Failure(Status::InvalidArgument, "Service contract, instance, operation and object request are required.");

        if (auto status = Checkpoint(); !status)
            return status;

        struct ServiceLease
        {
            const ARTestHostApiV0 &host;
            ARTestServiceHandle handle = nullptr;
            bool Release() noexcept
            {
                const auto value = std::exchange(handle, nullptr);
                if (!value)
                    return true;
                try
                {
                    host.release_service(host.host_context, value);
                    return true;
                }
                catch (...)
                {
                    return false;
                }
            }
            ~ServiceLease() noexcept
            {
                (void)Release();
            }
        } lease{m_host};

        auto outcome = [&]() -> Result {
            try
            {
                ErrorStorage error;
                const auto resolved = m_host.resolve_service(m_host.host_context, View(contract), View(instance), &lease.handle, &error.value);
                if (resolved != ARTEST_STATUS_OK)
                    return HostResult(resolved, error);
                if (!lease.handle)
                    return Result::Failure(Status::HostFailure, "Host returned an empty service handle.");

                struct Capture
                {
                    std::optional<Json> data;
                    unsigned calls = 0;
                    ARTestStatus status = ARTEST_STATUS_OK;
                    static ARTestStatus ARTEST_ABI_CALL
                    Write(void *state, const ARTestPayloadView *payload,
                          ARTestErrorBuffer *outputError) noexcept
                    {
                        if (!state)
                            return ARTEST_STATUS_INVALID_ARGUMENT;
                        auto &self = *static_cast<Capture *>(state);
                        self.status = Boundary(outputError, [&]() -> ARTestStatus {
                            if (++self.calls != 1 || !payload)
                                return Fail(outputError, Status::HostFailure,
                                            "Service must return at most one valid payload.");
                            auto data = Parse(payload);
                            if (!data.is_object())
                                return Fail(outputError, Status::HostFailure,
                                            "Service result must be an object.");
                            self.data = std::move(data);
                            return ARTEST_STATUS_OK;
                        });
                        return self.status;
                    }
                } capture;
                ARTestResultSinkV0 sink{sizeof(ARTestResultSinkV0), 0, &capture, &Capture::Write};
                const auto text = request.dump();
                const auto payload = Payload(text);
                const auto invoked =
                    m_host.invoke_service(m_host.host_context, lease.handle, View(operation),
                                          &payload, &m_invocation, &sink, &error.value);
                if (invoked != ARTEST_STATUS_OK)
                    return HostResult(invoked, error);
                if (capture.status != ARTEST_STATUS_OK)
                    return HostResult(capture.status, error);
                return capture.data ? Result::WithData(std::move(*capture.data))
                                    : Result::Success();
            }
            catch (...)
            {
                return Result::Failure(Status::HostFailure,
                                       "The host service callback threw or returned invalid data.");
            }
        }();
        if (!lease.Release())
            return Result::Failure(Status::HostFailure,
                                   outcome.Succeeded()
                                       ? "The host service release callback threw."
                                       : outcome.Message() +
                                             "; the host service release callback also threw.");
        return outcome;
    }

};
} // namespace artest::sdk::detail

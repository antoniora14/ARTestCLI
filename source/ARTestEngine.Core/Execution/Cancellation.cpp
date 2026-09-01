#include "Cancellation.h"

#include <algorithm>
#include <thread>

namespace artest
{
    CancellationToken::CancellationToken(
        std::shared_ptr<SharedState> state,
        std::optional<std::chrono::steady_clock::time_point> deadline) noexcept
        : m_state(std::move(state)), m_deadline(deadline)
    {
    }

    bool CancellationToken::IsCancellationRequested() const noexcept
    {
        return m_state && m_state->cancellationRequested.load(std::memory_order_acquire);
    }

    bool CancellationToken::IsTimedOut() const noexcept
    {
        return m_deadline.has_value() && std::chrono::steady_clock::now() >= *m_deadline;
    }

    CancellationReason CancellationToken::Reason() const noexcept
    {
        if (IsCancellationRequested())
        {
            return CancellationReason::Requested;
        }
        return IsTimedOut() ? CancellationReason::TimedOut : CancellationReason::None;
    }

    bool CancellationToken::WaitFor(std::chrono::milliseconds duration) const
    {
        if (Reason() != CancellationReason::None)
        {
            return true;
        }

        const auto requestedEnd = std::chrono::steady_clock::now() + duration;
        const auto waitEnd = m_deadline.has_value()
            ? std::min(requestedEnd, *m_deadline)
            : requestedEnd;

        if (!m_state)
        {
            std::this_thread::sleep_until(waitEnd);
            return Reason() != CancellationReason::None;
        }

        std::unique_lock lock{m_state->mutex};
        m_state->condition.wait_until(lock, waitEnd, [this]
        {
            return m_state->cancellationRequested.load(std::memory_order_acquire);
        });
        return Reason() != CancellationReason::None;
    }

    CancellationToken CancellationToken::WithTimeout(std::chrono::milliseconds timeout) const
    {
        if (timeout.count() <= 0)
        {
            return *this;
        }

        const auto requestedDeadline = std::chrono::steady_clock::now() + timeout;
        const auto effectiveDeadline = m_deadline.has_value()
            ? std::min(*m_deadline, requestedDeadline)
            : requestedDeadline;
        return CancellationToken{m_state, effectiveDeadline};
    }

    CancellationSource::CancellationSource()
        : m_state(std::make_shared<CancellationToken::SharedState>())
    {
    }

    CancellationToken CancellationSource::Token() const noexcept
    {
        return CancellationToken{m_state, std::nullopt};
    }

    void CancellationSource::Cancel() noexcept
    {
        m_state->cancellationRequested.store(true, std::memory_order_release);
        m_state->condition.notify_all();
    }

    bool CancellationSource::IsCancellationRequested() const noexcept
    {
        return m_state->cancellationRequested.load(std::memory_order_acquire);
    }
}

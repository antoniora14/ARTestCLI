#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

namespace artest
{
    enum class CancellationReason
    {
        None,
        Requested,
        TimedOut
    };

    class CancellationSource;

    class CancellationToken final
    {
    public:
        CancellationToken() = default;

        [[nodiscard]] bool IsCancellationRequested() const noexcept;
        [[nodiscard]] bool IsTimedOut() const noexcept;
        [[nodiscard]] CancellationReason Reason() const noexcept;
        [[nodiscard]] bool WaitFor(std::chrono::milliseconds duration) const;
        [[nodiscard]] CancellationToken WithTimeout(std::chrono::milliseconds timeout) const;
        // Host adapters may project this deadline into their own transport contract.
        // Core remains unaware of ABI layouts and platform-specific clock callbacks.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> Deadline() const noexcept
        {
            return m_deadline;
        }

    private:
        struct SharedState
        {
            std::atomic_bool cancellationRequested{false};
            mutable std::mutex mutex;
            std::condition_variable condition;
        };

        CancellationToken(
            std::shared_ptr<SharedState> state,
            std::optional<std::chrono::steady_clock::time_point> deadline) noexcept;

        std::shared_ptr<SharedState> m_state;
        std::optional<std::chrono::steady_clock::time_point> m_deadline;

        friend class CancellationSource;
    };

    class CancellationSource final
    {
    public:
        CancellationSource();

        [[nodiscard]] CancellationToken Token() const noexcept;
        void Cancel() noexcept;
        [[nodiscard]] bool IsCancellationRequested() const noexcept;

    private:
        std::shared_ptr<CancellationToken::SharedState> m_state;
    };
}

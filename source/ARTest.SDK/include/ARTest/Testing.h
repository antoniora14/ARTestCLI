#pragma once

#include "Context.h"
#include <functional>
#include <vector>

namespace artest::sdk::testing
{
// Deterministic, no Engine, DLL, sleep or hardware. Test callbacks execute locally.
class TestContext final : public Context
{
  public:
    struct LogEntry
    {
        LogLevel level;
        std::string message;
    };
    struct ServiceCall
    {
        std::string contract, instance, operation;
        Json request;
    };
    std::string instrumentId;
    bool cancelled = false;
    std::chrono::milliseconds elapsed{0};
    std::optional<std::chrono::milliseconds> deadline;
    std::vector<LogEntry> logs;
    std::vector<ServiceCall> calls;
    std::function<Result(const ServiceCall &)> onCall;

    std::string_view InstrumentId() const noexcept override
    {
        return instrumentId;
    }
    Result Checkpoint() const override
    {
        if (deadline && elapsed >= *deadline)
            return Result::Failure(Status::TimedOut, "Test deadline expired.");
        if (cancelled)
            return Result::Failure(Status::Cancelled, "Test invocation cancelled.");
        return Result::Success();
    }
    Result WaitFor(std::chrono::milliseconds duration) override
    {
        if (duration.count() < 0 || elapsed.count() < 0 ||
            duration > (std::chrono::milliseconds::max)() - elapsed)
            return Result::Failure(Status::InvalidArgument, "Invalid test wait duration.");
        if (auto result = Checkpoint(); !result)
            return result;
        elapsed += duration;
        return Checkpoint();
    }
    void Log(LogLevel level, std::string_view message) override
    {
        logs.push_back({level, std::string{message}});
    }
    Result Call(std::string_view contract, std::string_view instance, std::string_view operation,
                const Json &request) override
    {
        if (auto result = Checkpoint(); !result)
            return result;
        ServiceCall call{std::string{contract}, std::string{instance}, std::string{operation},
                         request};
        calls.push_back(call);
        return onCall ? onCall(call)
                      : Result::Failure(Status::NotFound, "No test service callback configured.");
    }
};
} // namespace artest::sdk::testing

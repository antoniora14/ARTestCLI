#pragma once

#include "Result.h"
#include <chrono>
#include <string_view>

namespace artest::sdk
{
enum class LogLevel
{
    Trace,
    Information,
    Warning,
    Error
};

// Implement a fake Context for pure C++ unit tests. The native implementation
// is call-scoped and must never escape an invocation or cross a module boundary.
class Context
{
  public:
    virtual ~Context() = default;
    [[nodiscard]] virtual std::string_view InstrumentId() const noexcept = 0;
    [[nodiscard]] virtual Result Checkpoint() const = 0;
    [[nodiscard]] virtual Result WaitFor(std::chrono::milliseconds duration) = 0;
    virtual void Log(LogLevel level, std::string_view message) = 0;
    [[nodiscard]] virtual Result Call(std::string_view contract, std::string_view instance, std::string_view operation, const Json &request) = 0;
    [[nodiscard]] Result CallInstrument(std::string_view contract, std::string_view operation, const Json &request = Json::object())
    {
        if (InstrumentId().empty())
            return Result::Failure(Status::InvalidArgument, "The command has no configured instrument.");

        return Call(contract, InstrumentId(), operation, request);
    }
};
} // namespace artest::sdk

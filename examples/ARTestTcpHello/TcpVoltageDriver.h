#pragma once
#include "tcp/Transport.h"
#include <ARTest/InstrumentDriver.h>
#include <cmath>
#include <memory>

namespace artest::tcphello
{
inline constexpr auto Contract = "artest.contract.example.tcp-voltage.v1";
inline constexpr auto Apply = "artest.example.tcp-voltage.v1/apply";
inline constexpr auto Read = "artest.example.tcp-voltage.v1/read";
inline constexpr auto MeasurementSchema = "artest.schema.example.tcp-voltage.v1";

class TcpVoltageDriver final : public sdk::InstrumentDriver
{
    std::unique_ptr<Winsock> winsock_;
    Socket socket_;
    std::timed_mutex mutex_;
    std::uint64_t sequence_ = 0;
    int operationMs_ = 1000;
    int shutdownMs_ = 500;

    sdk::Result Exchange(const char* operation, double voltage, const Budget& budget)
    {
        // Called with the instance transaction lock held, including cleanup.
        bool sent = false;
        const bool mutating = std::string_view(operation) == "apply";
        try
        {
            if (!socket_) return sdk::Result::Failure(sdk::Status::InvalidState, "TCP connection is unavailable; start a new session.");
            const auto id = ++sequence_;
            sdk::Json request{{"v", 1}, {"id", id}, {"op", operation}};
            if (mutating) request["value"] = voltage;
            const auto frame = request.dump() + "\n";
            Send(socket_.Get(), frame, budget, sent);
            const auto response = sdk::Json::parse(Receive(socket_.Get(), budget));
            if (!response.is_object() || !response.at("v").is_number_unsigned() ||
                !response.at("id").is_number_unsigned() || !response.at("ok").is_boolean() ||
                response.at("v") != 1 || response.at("id") != id ||
                response.at("op") != operation || response.at("ok") != true)
                throw Failure(sdk::Status::ExtensionFailure, "TCP response does not acknowledge this operation.");
            if (mutating || std::string_view(operation) == "read")
            {
                if (!response.at("value").is_number() || response.at("unit") != "V")
                    throw Failure(sdk::Status::ExtensionFailure, "Invalid TCP voltage payload.");
                const auto value = response.at("value").get<double>();
                if (!std::isfinite(value) || value < 0 || value > 60)
                    throw Failure(sdk::Status::ExtensionFailure, "TCP voltage is out of range.");
                budget.Check();
                return sdk::Result::WithData({{"value", value}, {"unit", "V"}}, MeasurementSchema);
            }
            budget.Check();
            return sdk::Result::Success();
        }
        catch (const std::exception& error)
        {
            socket_.Reset(); // Never reuse a late response or silently reconnect/replay.
            const auto* failure = dynamic_cast<const Failure*>(&error);
            const auto cause = failure ? failure->cause : sdk::Status::ExtensionFailure;
            return mutating && sent ? sdk::Result::Indeterminate(error.what(), cause)
                                    : sdk::Result::Failure(cause, error.what());
        }
    }

    sdk::Result Invoke(const char* operation, const sdk::Parameters& parameters, sdk::Context& context)
    {
        const double voltage = std::string_view(operation) == "apply" ? parameters.Get<double>("voltage") : 0;
        if (!std::isfinite(voltage) || voltage < 0 || voltage > 60)
            return sdk::Result::Failure(sdk::Status::InvalidArgument, "voltage must be finite and within 0..60 V.");
        try
        {
            Budget budget(operationMs_, [&context] { return context.Checkpoint(); });
            auto lock = budget.Lock(mutex_);
            return Exchange(operation, voltage, budget);
        }
        catch (const Failure& error) { return sdk::Result::Failure(error.cause, error.what()); }
    }
public:
    TcpVoltageDriver()
    {
        RegisterOperation(Apply, [this](const sdk::Parameters& p, sdk::Context& c) { return Invoke("apply", p, c); });
        RegisterOperation(Read, [this](const sdk::Parameters& p, sdk::Context& c) { return Invoke("read", p, c); });
    }
    sdk::Result Initialize(const sdk::Parameters& config, sdk::Context& context) override
    {
        const auto port = config.Get<int>("port");
        const auto initializeMs = config.Optional<int>("initializeMs", 1000);
        operationMs_ = config.Optional<int>("operationMs", 1000);
        shutdownMs_ = config.Optional<int>("shutdownMs", 500);
        if (port < 1 || port > 65535 || initializeMs < 1 || initializeMs > 10000 ||
            operationMs_ < 1 || operationMs_ > 10000 || shutdownMs_ < 1 || shutdownMs_ > 10000)
            return sdk::Result::Failure(sdk::Status::InvalidArgument, "Invalid port or TCP budget (1..10000 ms).");
        try
        {
            Budget budget(initializeMs, [&context] { return context.Checkpoint(); });
            auto lock = budget.Lock(mutex_);
            winsock_ = std::make_unique<Winsock>();
            socket_ = Connect(static_cast<unsigned short>(port), budget);
            return Exchange("hello", 0, budget);
        }
        catch (const Failure& error) { socket_.Reset(); return sdk::Result::Failure(error.cause, error.what()); }
    }
    sdk::Result Shutdown(sdk::Context& context) override
    {
        context.Log(sdk::LogLevel::Information, "TCP_HELLO_CLEANUP_ATTEMPTED");
        try
        {
            Budget budget(shutdownMs_, [&context] { return context.Checkpoint(); });
            auto lock = budget.Lock(mutex_);
            auto result = socket_ ? Exchange("close", 0, budget) :
                sdk::Result::Failure(sdk::Status::InvalidState, "TCP disconnected: remote close is unconfirmed.");
            socket_.Reset();
            winsock_.reset();
            return result;
        }
        catch (const Failure& error)
        {
            // Cancellation still releases locally owned resources when no transaction is active.
            // Never close a socket owned by an in-flight transaction on a lock timeout.
            if (mutex_.try_lock())
            {
                socket_.Reset();
                winsock_.reset();
                mutex_.unlock();
            }
            return sdk::Result::Failure(error.cause, error.what());
        }
    }
};
} // namespace artest::tcphello

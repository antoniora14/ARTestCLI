#pragma once
#include <ARTest/Extension.h>
#include <ARTest/Metadata.h>
#include <fstream>
#include <thread>

namespace artest::tests::effects
{
using namespace artest::sdk;
inline constexpr auto Contract = "artest.contract.instrument.power-supply.v1";
inline constexpr auto TurnOn = "artest.instrument.power-supply.v1/turn-on";

inline void Record(const std::string &path, const char *value)
{
    std::ofstream stream(path, std::ios::app);
    stream << value << '\n';
    stream.flush();
    if (!stream) throw std::runtime_error("Cannot record simulated effect");
}
class Driver final : public InstrumentDriver
{
    std::string file, mode, relay;
    bool cleanupFails = false;
    Result Write(Context &context)
    {
        Record(file + ".calls", "call");
        if (mode == "before") return Result::Failure(Status::ResourceUnavailable, "Confirmed pre-send failure");
        if (mode == "relay")
            return context.Call(Contract, relay, TurnOn, {{"channel", 1}});
        Record(file, "effect"); // The simulator, not the caller, owns the effect record.
        if (mode == "ok") return Result::Success();
        if (mode == "late")
            std::this_thread::sleep_for(std::chrono::milliseconds{150});
        if (mode == "hold")
        {
            while (context.Checkpoint())
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        const auto cause = mode == "timeout" || mode == "late" ? Status::TimedOut :
                           mode == "cancelled" || mode == "hold" ? Status::Cancelled : Status::ExtensionFailure;
        return Result::Indeterminate("C01 native lost acknowledgement", cause);
    }
  public:
    Driver()
    {
        RegisterOperation(TurnOn, [this](const Parameters &, Context &context) { return Write(context); });
        RegisterOperation("artest.instrument.power-supply.v1/set-voltage",
            [](const Parameters &, Context &) { return Result::Success(); });
        RegisterOperation("artest.instrument.power-supply.v1/turn-off",
            [](const Parameters &, Context &) { return Result::Success(); });
    }
    Result Initialize(const Parameters &p, Context &) override
    {
        file = p.Get<std::string>("effectFile");
        mode = p.Optional<std::string>("mode", "lost");
        relay = p.Optional<std::string>("relay", "");
        cleanupFails = p.Optional<bool>("failShutdown", false);
        return Result::Success();
    }
    Result Shutdown(Context &) override
    {
        Record(file + ".cleanup", "cleanup");
        return cleanupFails ? Result::Failure(Status::ExtensionFailure, "C01 cleanup failed")
                            : Result::Success();
    }
};
class Command final : public sdk::Command
{
  public:
    Result Execute(const Parameters &p, Context &context) override
    {
        auto result = context.CallInstrument(Contract, TurnOn, {{"channel", 1}});
        if (result) return result;
        const auto action = p.Optional<std::string>("action", "propagate");
        if (action == "wrap") return Result::Failure(Status::ExtensionFailure, "C01 wrapped command failure");
        if (action == "throw") throw std::runtime_error("C01 command threw after service");
        if (action == "retry")
        {
            // Deliberately broken author code: the broker must reject this replay.
            (void)context.CallInstrument(Contract, TurnOn, {{"channel", 1}});
            return Result::Success("C01 swallowed failure");
        }
        return result;
    }
};
inline Extension Define()
{
    Extension definition{"com.artest.test.effects", "0.1.0", "C-01 fault doubles", "ARTest tests"};
    definition.AddDriver<Driver>({.id = "com.artest.test.driver.effects", .name = "Test-only effect driver",
        .contract = Contract, .mode = DriverMode::Simulated,
        .metadata = {.schema = Schema::Object().Required("effectFile", Schema::String())
            .Optional("mode", Schema::String()).Optional("relay", Schema::String())
            .Optional("failShutdown", Schema::Boolean())}});
    definition.AddCommand<Command>({.id = "com.artest.test.command.effects", .name = "Test-only effect command",
        .metadata = {.schema = Schema::Object().Optional("action", Schema::String()), .requiredContracts = {Contract}}});
    return definition;
}
}

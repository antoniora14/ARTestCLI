#include "SimPowerDriver.h"

namespace artest::extensions
{
using sdk::Context;
using sdk::Parameters;
using sdk::Result;
using sdk::Status;

namespace
{
int Channel(const Parameters &parameters)
{
    const auto channel = parameters.Optional<int>("channel", -1);
    if (channel < 0)
        throw std::invalid_argument("channel must be zero or greater.");
    return channel;
}
} // namespace

SimPowerDriver::SimPowerDriver() : SimPowerDriver(false) {}

SimPowerDriver::SimPowerDriver(bool requireResource) : m_requireResource(requireResource)
{
    // Registration is local and metadata-only; constructors never open instruments.
    RegisterOperation("artest.instrument.power-supply.v1/set-voltage",
                      [this](const Parameters &p, Context &) { return SetVoltage(p); });
    RegisterOperation("artest.instrument.power-supply.v1/set-current-limit",
                      [this](const Parameters &p, Context &) { return SetCurrentLimit(p); });
    RegisterOperation("artest.instrument.power-supply.v1/turn-on",
                      [this](const Parameters &p, Context &) { return TurnOn(p); });
    RegisterOperation("artest.instrument.power-supply.v1/turn-off",
                      [this](const Parameters &p, Context &) { return TurnOff(p); });
    RegisterOperation("artest.instrument.power-supply.v1/read-state",
                      [this](const Parameters &p, Context &) { return ReadState(p); });
}

Result SimPowerDriver::Initialize(const Parameters &configuration, Context &context)
{
    // Read cleanup policy first so partial initialization can still report cleanup failure.
    m_failShutdown = configuration.Optional<bool>("failShutdown", false);
    m_remainingTurnOnFailures = configuration.Optional<int>("failTurnOnAttempts", 0);
    const bool failInitialize = configuration.Optional<bool>("failInitialize", false);
    const bool failInitialization = configuration.Optional<bool>("failInitialization", false);
    const bool missingResource =
        m_requireResource && configuration.Optional<std::string>("hw-rsrc", {}).empty();
    if (m_remainingTurnOnFailures < 0)
        return Result::Failure(Status::InvalidArgument, "failTurnOnAttempts must be zero or greater.");
    if (missingResource || failInitialize || failInitialization)
    {
        const std::string message = missingResource
            ? "[POWER_SUPPLY_RESOURCE_MISSING] Power supply resource is missing."
            : "Simulated initialization failure requested.";
        context.Log(sdk::LogLevel::Error, message);
        return Result::Failure(Status::ResourceUnavailable, message);
    }
    context.Log(sdk::LogLevel::Information, "Simulated power driver initialized.");
    return Result::Success();
}

Result SimPowerDriver::Shutdown(Context &context)
{
    // Always release local state, including after cancellation or partial initialization.
    m_outputs.clear();
    m_voltages.clear();
    context.Log(sdk::LogLevel::Information, "Simulated power driver shut down.");
    if (m_failShutdown)
        return Result::Failure(Status::ExtensionFailure, "Simulated shutdown failure was requested.");
    return Result::Success();
}

Result SimPowerDriver::SetVoltage(const Parameters &parameters)
{
    const auto channel = Channel(parameters);
    const auto voltage = parameters.Optional<double>("voltage", -1.0);
    if (voltage < 0.0)
        return Result::Failure(Status::InvalidArgument, "voltage must be zero or greater.");
    m_voltages[channel] = voltage;
    return Result::Success();
}

Result SimPowerDriver::SetCurrentLimit(const Parameters &parameters)
{
    (void)Channel(parameters);
    if (parameters.Optional<double>("currentLimit", -1.0) < 0.0)
        return Result::Failure(Status::InvalidArgument, "currentLimit must be zero or greater.");
    // Compatibility: the reference simulator validates this setting but does not model current.
    return Result::Success();
}

Result SimPowerDriver::TurnOn(const Parameters &parameters)
{
    const auto channel = Channel(parameters);
    if (m_remainingTurnOnFailures > 0)
    {
        --m_remainingTurnOnFailures;
        return Result::Failure(Status::ExtensionFailure,
                               "POWER_SUPPLY_TURN_ON_SIMULATED_FAILURE: Simulated turn-on failure.");
    }
    m_outputs[channel] = true;
    return Result::Success();
}

Result SimPowerDriver::TurnOff(const Parameters &parameters)
{
    m_outputs[Channel(parameters)] = false;
    return Result::Success();
}

Result SimPowerDriver::ReadState(const Parameters &parameters) const
{
    const auto channel = Channel(parameters);
    const auto voltage = m_voltages.find(channel);
    const auto output = m_outputs.find(channel);
    return Result::WithData(
        {{"channel", channel},
         {"voltage", voltage == m_voltages.end() ? 0.0 : voltage->second},
         {"outputOn", output != m_outputs.end() && output->second}},
        "artest.schema.instrument.power-supply.result.v1");
}
} // namespace artest::extensions

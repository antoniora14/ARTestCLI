#include "SimCanDriver.h"

namespace artest::extensions
{
using sdk::Context;
using sdk::Parameters;
using sdk::Result;
using sdk::Status;

SimCanDriver::SimCanDriver()
{
    RegisterOperation("artest.instrument.can.v1/send",
                      [this](const Parameters &p, Context &) { return Send(p); });
}

Result SimCanDriver::Initialize(const Parameters &configuration, Context &)
{
    m_failShutdown = configuration.Optional<bool>("failShutdown", false);
    const auto resource = configuration.Optional<std::string>("hw-rsrc", {});
    if (resource.empty() || configuration.Optional<bool>("failInitialize", false))
        return Result::Failure(Status::ResourceUnavailable,
                               "[CAN_RESOURCE_MISSING] CAN resource is missing or unavailable.");
    return Result::Success();
}

Result SimCanDriver::Shutdown(Context &)
{
    m_messageCount = 0;
    if (m_failShutdown)
        return Result::Failure(Status::ExtensionFailure, "Simulated CAN cleanup failure.");
    return Result::Success();
}

Result SimCanDriver::Send(const Parameters &parameters)
{
    const auto dlc = parameters.Get<int>("dlc");
    const auto bytes = parameters.Get<sdk::Json>("data");
    const auto idText = parameters.Get<std::string>("id");
    // Preserve decimal/hex/octal parsing and the existing 29-bit CAN identifier limit.
    // Malformed and overflowing identifiers are input failures, not successful sends.
    std::size_t consumed = 0;
    unsigned long id = 0;
    try
    {
        id = std::stoul(idText, &consumed, 0);
    }
    catch (const std::invalid_argument &)
    {
        return Result::Failure(Status::InvalidArgument, "Invalid CAN identifier.");
    }
    catch (const std::out_of_range &)
    {
        return Result::Failure(Status::InvalidArgument, "CAN identifier is out of range.");
    }
    if (consumed != idText.size() || id > 0x1fffffffUL || dlc < 0 || dlc > 8 ||
        !bytes.is_array() || bytes.size() != static_cast<std::size_t>(dlc))
        return Result::Failure(Status::InvalidArgument, "Invalid CAN identifier, DLC or data length.");
    for (const auto &byte : bytes)
        if (!byte.is_number_integer() || byte < 0 || byte > 255)
            return Result::Failure(Status::InvalidArgument, "CAN data bytes must be integers in 0..255.");

    // Only the count is observable; do not retain an unbounded history of simulated frames.
    ++m_messageCount;
    return Result::WithData({{"sent", true}, {"messageCount", m_messageCount}});
}
} // namespace artest::extensions

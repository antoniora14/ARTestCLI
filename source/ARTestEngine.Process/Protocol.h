#pragma once
#include "artest_process.pb.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace artest::process
{
namespace wire = v0;
inline constexpr std::uint32_t ProtocolMajor = 0, ProtocolMinor = 1;
inline constexpr std::size_t MaxFrameBytes = 1024 * 1024;
class ProcessError : public std::runtime_error
{
  public:
    ProcessError(std::string code, std::string detail)
        : std::runtime_error(std::move(detail)), code(std::move(code)) {}
    std::string code;
};
void Validate(const wire::Envelope &message);
std::string Encode(const wire::Envelope &message);
wire::Envelope Decode(std::string_view bytes);
wire::Envelope Message(std::uint64_t generation, std::uint64_t correlation);
wire::Response Response(wire::Status status, std::string diagnostic = {});
} // namespace artest::process

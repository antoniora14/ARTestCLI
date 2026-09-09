#include "Protocol.h"
#include "../ThirdParty/json.hpp"

namespace artest::process
{
namespace
{
void Check(bool valid, const char *message)
{
    if (!valid) throw ProcessError("PROCESS_PROTOCOL_INVALID", message);
}
void CheckPayload(const wire::Payload &payload)
{
    Check(payload.schema_id().size() <= 256, "Schema ID is too long.");
    Check(payload.json().size() <= MaxFrameBytes, "Payload exceeds the control-plane limit.");
    if (!payload.json().empty())
    {
        Check(!payload.schema_id().empty(), "JSON requires a schema ID.");
        auto parsed = nlohmann::json::parse(payload.json(), nullptr, false);
        Check(!parsed.is_discarded(), "Malformed or non-finite JSON payload.");
    }
}
}
void Validate(const wire::Envelope &message)
{
    Check(message.major() == ProtocolMajor && message.minor() == ProtocolMinor, "Unsupported wire version.");
    Check(message.generation() != 0 && message.correlation() != 0, "Missing generation/correlation.");
    Check(message.ByteSizeLong() <= MaxFrameBytes, "Message exceeds the control-plane limit.");
    switch (message.body_case())
    {
    case wire::Envelope::kHello:
        Check(!message.hello().extension_id().empty() && message.hello().extension_id().size() <= 256,
              "Missing/oversized extension ID.");
        Check(message.hello().fingerprint().size() == 64 && message.hello().nonce().size() == 32,
              "Invalid fingerprint or bootstrap nonce.");
        break;
    case wire::Envelope::kRequest:
        Check(wire::Operation_IsValid(message.request().operation()) &&
              message.request().operation() != wire::OPERATION_UNSPECIFIED, "Unknown operation.");
        Check(message.request().type_id().size() <= 256 && message.request().operation_id().size() <= 256,
              "Oversized request identity.");
        CheckPayload(message.request().payload());
        break;
    case wire::Envelope::kResponse:
        Check(wire::Status_IsValid(message.response().status()), "Unknown response status.");
        Check(!message.response().effect_indeterminate() || message.response().status() != wire::OK,
              "An indeterminate effect cannot report technical success.");
        Check(message.response().diagnostic().size() <= 4096, "Oversized diagnostic.");
        CheckPayload(message.response().payload());
        break;
    case wire::Envelope::kCancel: break;
    case wire::Envelope::kEvent:
        Check(message.event().message().size() <= 4096 && message.event().category().size() <= 256 &&
              message.event().severity() <= 2, "Invalid event.");
        break;
    default: throw ProcessError("PROCESS_PROTOCOL_INVALID", "Missing or unknown message body.");
    }
}
std::string Encode(const wire::Envelope &message)
{
    Validate(message);
    return message.SerializeAsString();
}
wire::Envelope Decode(std::string_view bytes)
{
    Check(!bytes.empty() && bytes.size() <= MaxFrameBytes, "Invalid frame size.");
    wire::Envelope message;
    Check(message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())), "Malformed protobuf.");
    Validate(message);
    return message;
}
wire::Envelope Message(std::uint64_t generation, std::uint64_t correlation)
{
    wire::Envelope message;
    message.set_major(ProtocolMajor);
    message.set_minor(ProtocolMinor);
    message.set_generation(generation);
    message.set_correlation(correlation);
    return message;
}
wire::Response Response(wire::Status status, std::string diagnostic)
{
    wire::Response response;
    response.set_status(status);
    response.set_diagnostic(std::move(diagnostic));
    return response;
}
} // namespace artest::process

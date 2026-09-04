#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace artest::sdk
{
using Json = nlohmann::json;

// Operation status, not a measurement verdict. These values project native ABI 0.1.
enum class Status : std::int32_t
{
    Ok = 0,
    InvalidArgument = 1,
    IncompatibleAbi = 2,
    BufferTooSmall = 3,
    NotFound = 4,
    AlreadyExists = 5,
    InvalidState = 6,
    OperationNotSupported = 7,
    Cancelled = 8,
    TimedOut = 9,
    ResourceUnavailable = 10,
    ExtensionFailure = 11,
    HostFailure = 12,
    InternalFailure = 13
};

class [[nodiscard]] Result final
{
  public:
    static Result Success()
    {
        return Result{Status::Ok, {}};
    }
    static Result Success(std::string message)
    {
        return WithData(Json{{"message", std::move(message)}});
    }
    static Result WithData(Json data)
    {
        return WithData(std::move(data), "artest.schema.generic-json.v1");
    }
    // Preserve a service's declared payload schema without exposing ABI structures.
    static Result WithData(Json data, std::string schemaId)
    {
        if (schemaId.empty() || schemaId.size() > 1024 * 1024 ||
            schemaId.find('\0') != std::string::npos)
            throw std::invalid_argument("A valid result schema ID is required.");
        if (!data.is_object() || (data.contains("message") && !data["message"].is_string()))
            throw std::invalid_argument(
                "Result data must be an object; message, if present, must be a string.");
        auto result = Success();
        // The local C++ diagnostic and its serialized ABI representation must agree.
        if (data.contains("message"))
            result.m_message = data["message"].get<std::string>();
        result.m_data = std::move(data);
        result.m_schemaId = std::move(schemaId);
        return result;
    }
    static Result Failure(Status status, std::string message)
    {
        if (status <= Status::Ok || status > Status::InternalFailure)
            throw std::invalid_argument("A failure must have a recognized non-success status.");
        return Result{status, std::move(message)};
    }
    [[nodiscard]] bool Succeeded() const noexcept
    {
        return m_status == Status::Ok;
    }
    explicit operator bool() const noexcept
    {
        return Succeeded();
    }
    [[nodiscard]] Status Code() const noexcept
    {
        return m_status;
    }
    [[nodiscard]] const std::string &Message() const noexcept
    {
        return m_message;
    }
    [[nodiscard]] const std::optional<Json> &Data() const noexcept
    {
        return m_data;
    }
    [[nodiscard]] const std::string &SchemaId() const noexcept
    {
        return m_schemaId;
    }

  private:
    Result(Status status, std::string message) : m_status(status), m_message(std::move(message))
    {
    }
    Status m_status;
    std::string m_message;
    std::optional<Json> m_data;
    std::string m_schemaId = "artest.schema.generic-json.v1";
};
} // namespace artest::sdk

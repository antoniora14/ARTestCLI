#pragma once

#include "../../ARTestExtensionAbi.h"
#include "../Result.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace artest::sdk::detail
{
static_assert(static_cast<ARTestStatus>(Status::InternalFailure) == ARTEST_STATUS_INTERNAL_FAILURE);
inline ARTestStringView View(std::string_view text) noexcept
{
    return {text.data(), text.size()};
}

inline bool ValidErrorBuffer(const ARTestErrorBuffer *error) noexcept
{
    return !error || (error->struct_size >= sizeof(*error) && error->reserved == 0 &&
                      (error->data || error->capacity == 0));
}

inline ARTestStatus Fail(ARTestErrorBuffer *error, Status status, std::string_view message) noexcept
{
    if (!ValidErrorBuffer(error))
        return ARTEST_STATUS_INVALID_ARGUMENT;
    if (error)
    {
        error->required_size = message.size() + 1;
        // Never return a truncated diagnostic that could misrepresent a hardware failure.
        if (!error->data || error->capacity < error->required_size)
        {
            if (error->data && error->capacity)
                error->data[0] = '\0';
            return ARTEST_STATUS_BUFFER_TOO_SMALL;
        }
        if (!message.empty())
            std::memcpy(error->data, message.data(), message.size());
        error->data[message.size()] = '\0';
    }
    return static_cast<ARTestStatus>(status);
}

template <class F> ARTestStatus Boundary(ARTestErrorBuffer *error, F &&function) noexcept
{
    if (!ValidErrorBuffer(error))
        return ARTEST_STATUS_INVALID_ARGUMENT;
    if (error)
    {
        error->required_size = 0;
        if (error->data && error->capacity)
            error->data[0] = '\0';
    }
    try
    {
        return function();
    }
    catch (const Json::exception &e)
    {
        return Fail(error, Status::InvalidArgument, e.what());
    }
    catch (const std::invalid_argument &e)
    {
        return Fail(error, Status::InvalidArgument, e.what());
    }
    catch (const std::exception &e)
    {
        return Fail(error, Status::ExtensionFailure, e.what());
    }
    catch (...)
    {
        return Fail(error, Status::ExtensionFailure, "Unhandled extension exception.");
    }
}

inline std::string_view Text(ARTestStringView value)
{
    if ((!value.data && value.size) || value.size > 1024 * 1024)
        throw std::invalid_argument("Invalid ABI string view.");
    const auto text = value.size ? std::string_view{value.data, value.size} : std::string_view{};
    if (text.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Identifiers must not contain null bytes.");
    return text;
}

inline Json Parse(const ARTestPayloadView *value)
{
    if (!value)
        return Json::object();
    if (value->struct_size < sizeof(*value) ||
        value->encoding != ARTEST_PAYLOAD_ENCODING_JSON_UTF8 ||
        (!value->bytes.data && value->bytes.size) || value->bytes.size > 16 * 1024 * 1024)
        throw std::invalid_argument("A valid JSON UTF-8 payload of at most 16 MiB is required.");
    if (!value->bytes.size)
        return Json::object();
    // Bound nesting before the DOM parser allocates or descends recursively.
    std::size_t depth = 0;
    bool quoted = false, escaped = false;
    for (std::size_t index = 0; index < value->bytes.size; ++index)
    {
        const auto ch = static_cast<char>(value->bytes.data[index]);
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                quoted = false;
        }
        else if (ch == '"')
            quoted = true;
        else if (ch == '{' || ch == '[')
        {
            if (++depth > 64)
                throw std::invalid_argument("Payload nesting exceeds 64 levels.");
        }
        else if ((ch == '}' || ch == ']') && depth)
            --depth;
    }
    return Json::parse(value->bytes.data, value->bytes.data + value->bytes.size);
}

inline ARTestPayloadView Payload(const std::string &text) noexcept
{
    return {sizeof(ARTestPayloadView),
            ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View("artest.schema.generic-json.v1"),
            View("application/json; charset=utf-8"),
            {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()}};
}

inline bool ValidSink(const ARTestResultSinkV0 *sink) noexcept
{
    return !sink || (sink->struct_size >= sizeof(*sink) && !sink->reserved && sink->write);
}
inline ARTestStatus Return(const Result &result, const ARTestResultSinkV0 *sink,
                           ARTestErrorBuffer *error)
{
    if (!result)
        return Fail(error, result.Code(), result.Message());
    if (!ValidSink(sink))
        return Fail(error, Status::InvalidArgument, "Invalid result sink.");
    if (!sink || !result.Data())
        return ARTEST_STATUS_OK;
    const auto text = result.Data()->dump();
    auto payload = Payload(text);
    payload.schema_id = View(result.SchemaId());
    try
    {
        return sink->write(sink->sink_context, &payload, error);
    }
    catch (...)
    {
        return Fail(error, Status::HostFailure, "The host result callback threw.");
    }
}

struct ErrorStorage
{
    std::array<char, 2048> text{};
    ARTestErrorBuffer value{sizeof(ARTestErrorBuffer), 0, text.data(), text.size(), 0};
    std::string Message() const
    {
        const auto end = std::find(text.begin(), text.end(), '\0');
        return text[0] ? std::string{text.begin(), end}
                       : std::string{"Host service failed without a diagnostic (or its diagnostic "
                                     "exceeded the buffer)."};
    }
};
inline Result HostResult(ARTestStatus code, const ErrorStorage &error)
{
    if (code == ARTEST_STATUS_OK)
        return Result::Success();
    const auto status =
        code >= ARTEST_STATUS_INVALID_ARGUMENT && code <= ARTEST_STATUS_INTERNAL_FAILURE
            ? static_cast<Status>(code)
            : Status::HostFailure;
    return Result::Failure(status, error.Message());
}
} // namespace artest::sdk::detail

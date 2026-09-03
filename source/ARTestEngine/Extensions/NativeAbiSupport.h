#pragma once
#include "../../ARTest.SDK/include/ARTestExtensionAbi.h"
#include <algorithm>
#include <string>
namespace artest::extensions
{
[[nodiscard]] inline std::string ToString(ARTestStringView value)
{
    return value.data == nullptr ? std::string{} : std::string{value.data, value.size};
}

[[nodiscard]] inline ARTestStringView View(const std::string &value) noexcept
{
    return {value.data(), value.size()};
}

[[nodiscard]] inline ARTestPayloadView JsonPayload(const std::string &value) noexcept
{
    static const std::string schema = "artest.schema.generic-json.v1";
    static const std::string media = "application/json; charset=utf-8";
    return {sizeof(ARTestPayloadView),
            ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema),
            View(media),
            {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()}};
}

inline void SetError(ARTestErrorBuffer *error, const std::string &message) noexcept
{
    if (error == nullptr)
        return;
    error->required_size = message.size() + 1U;
    if (error->data == nullptr || error->capacity == 0U)
        return;
    const auto count = (std::min)(message.size(), error->capacity - 1U);
    std::copy_n(message.data(), count, error->data);
    error->data[count] = '\0';
}

struct ErrorStorage
{
    char text[1024]{};
    ARTestErrorBuffer buffer{sizeof(ARTestErrorBuffer), 0U, text, sizeof(text), 0U};
    [[nodiscard]] inline std::string Message(std::string fallback) const
    {
        return text[0] == '\0' ? std::move(fallback) : std::string{text};
    }
};

} // namespace artest::extensions

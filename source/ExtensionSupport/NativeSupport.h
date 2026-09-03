#pragma once
#include "../ARTest.SDK/include/ARTestExtensionAbi.h"
#include "../ThirdParty/json.hpp"
#include <algorithm>
#include <string>

namespace artest::native
{
inline ARTestStringView View(const std::string &text) noexcept
{
    return {text.data(), text.size()};
}
inline std::string Text(ARTestStringView text)
{
    return text.data ? std::string{text.data, text.size} : std::string{};
}
inline void Error(ARTestErrorBuffer *error, const std::string &message) noexcept
{
    if (!error)
        return;
    error->required_size = message.size() + 1;
    if (!error->data || !error->capacity)
        return;
    const auto size = (std::min)(message.size(), error->capacity - 1);
    std::copy_n(message.data(), size, error->data);
    error->data[size] = '\0';
}
inline nlohmann::json Parse(const ARTestPayloadView *value)
{
    if (!value || value->struct_size < sizeof(ARTestPayloadView) ||
        value->encoding != ARTEST_PAYLOAD_ENCODING_JSON_UTF8 ||
        (!value->bytes.data && value->bytes.size))
        throw std::invalid_argument("A JSON payload is required.");
    if (!value->bytes.size)
        return nlohmann::json::object();
    return nlohmann::json::parse(value->bytes.data, value->bytes.data + value->bytes.size);
}
inline ARTestPayloadView Payload(const std::string &text)
{
    static const std::string schema = "artest.schema.generic-json.v1";
    static const std::string media = "application/json; charset=utf-8";
    return {sizeof(ARTestPayloadView),
            ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema),
            View(media),
            {reinterpret_cast<const uint8_t *>(text.data()), text.size()}};
}
inline bool Cancelled(const ARTestInvocationContextV0 *context) noexcept
{
    return context && context->is_cancellation_requested &&
           context->is_cancellation_requested(context->cancellation_context);
}
inline ARTestStatus Write(const ARTestResultSinkV0 *sink, const nlohmann::json &result,
                          ARTestErrorBuffer *error)
{
    if (!sink || !sink->write)
        return ARTEST_STATUS_OK;
    const auto text = result.dump();
    const auto value = Payload(text);
    return sink->write(sink->sink_context, &value, error);
}
template <typename Function>
ARTestStatus Guard(ARTestErrorBuffer *error, Function &&function) noexcept
{
    try
    {
        return function();
    }
    catch (const std::exception &exception)
    {
        Error(error, exception.what());
        return ARTEST_STATUS_EXTENSION_FAILURE;
    }
    catch (...)
    {
        Error(error, "Unhandled native extension failure.");
        return ARTEST_STATUS_EXTENSION_FAILURE;
    }
}
// Service handles never own driver allocation; release always returns to the host.
class ServiceLease final
{
  public:
    explicit ServiceLease(const ARTestHostApiV0 &host) : m_host(host)
    {
    }
    ~ServiceLease()
    {
        if (handle)
            m_host.release_service(m_host.host_context, handle);
    }
    ServiceLease(const ServiceLease &) = delete;
    ServiceLease &operator=(const ServiceLease &) = delete;
    ARTestServiceHandle handle = nullptr;

  private:
    const ARTestHostApiV0 &m_host;
};
} // namespace artest::native

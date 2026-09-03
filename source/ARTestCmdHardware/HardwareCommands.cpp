#define ARTEST_EXTENSION_EXPORTS
#include "../ExtensionSupport/NativeSupport.h"
#include <array>
#include <memory>

using namespace artest::native;
namespace
{
const std::string Id = "com.artest.extension.hardware-commands";
const std::string Version = "0.1.0";
const std::string Contract = "artest.contract.command.v1";
const std::string Power = "artest.contract.instrument.power-supply.v1";
const std::string Can = "artest.contract.instrument.can.v1";
const std::array<std::string, 3> Types{"com.artest.command.power.turn-on",
                                       "com.artest.command.power.turn-off",
                                       "com.artest.command.can.send"};
const std::array<std::string, 3> Names{"Power On", "Power Off", "Send CAN Message"};
} // namespace
struct ARTestExtensionOpaque
{
    ARTestHostApiV0 host{};
};
struct ARTestComponentOpaque
{
    std::size_t kind = 0;
};
namespace
{
ARTestStatus ARTEST_ABI_CALL Create(const ARTestHostApiV0 *host, const ARTestPayloadView *,
                                    ARTestExtensionHandle *out, ARTestErrorBuffer *error) noexcept
{
    if (!host || !out || host->struct_size < sizeof(*host) || !host->resolve_service ||
        !host->invoke_service || !host->release_service)
        return ARTEST_STATUS_INVALID_ARGUMENT;
    return Guard(error, [&]() -> ARTestStatus {
        *out = new ARTestExtensionOpaque{*host};
        return ARTEST_STATUS_OK;
    });
}
void ARTEST_ABI_CALL Destroy(ARTestExtensionHandle value) noexcept
{
    delete value;
}
std::size_t ARTEST_ABI_CALL Count(ARTestExtensionHandle) noexcept
{
    return Types.size();
}
ARTestStatus ARTEST_ABI_CALL Describe(ARTestExtensionHandle, std::size_t i,
                                      ARTestComponentDescriptorV0 *out,
                                      ARTestErrorBuffer *) noexcept
{
    if (i >= Types.size() || !out || out->struct_size < sizeof(*out))
        return ARTEST_STATUS_INVALID_ARGUMENT;
    *out = {sizeof(*out),
            ARTEST_COMPONENT_KIND_COMMAND,
            ARTEST_COMPONENT_FLAG_NONE,
            View(Types[i]),
            View(Contract),
            View(Version),
            View(Names[i]),
            {}};
    return ARTEST_STATUS_OK;
}
ARTestStatus ARTEST_ABI_CALL Make(ARTestExtensionHandle, ARTestStringView type,
                                  const ARTestPayloadView *, ARTestComponentHandle *out,
                                  ARTestErrorBuffer *error) noexcept
{
    if (!out)
        return ARTEST_STATUS_INVALID_ARGUMENT;
    return Guard(error, [&]() -> ARTestStatus {
        for (std::size_t i = 0; i < Types.size(); ++i)
            if (Text(type) == Types[i])
            {
                *out = new ARTestComponentOpaque{i};
                return ARTEST_STATUS_OK;
            }
        return ARTEST_STATUS_NOT_FOUND;
    });
}
void ARTEST_ABI_CALL Drop(ARTestExtensionHandle, ARTestComponentHandle value) noexcept
{
    delete value;
}
ARTestStatus ARTEST_ABI_CALL Invoke(ARTestExtensionHandle extension,
                                    ARTestComponentHandle component, ARTestStringView operation,
                                    const ARTestPayloadView *payload,
                                    const ARTestInvocationContextV0 *cancellation,
                                    const ARTestResultSinkV0 *sink,
                                    ARTestErrorBuffer *error) noexcept
{
    if (!extension || !component)
        return ARTEST_STATUS_INVALID_ARGUMENT;
    return Guard(error, [&]() -> ARTestStatus {
        const auto request = Parse(payload);
        const auto &parameters = request.at("parameters");
        const auto instrument = request.at("instrumentId").get<std::string>();
        if (instrument.empty() || !parameters.is_object())
            return ARTEST_STATUS_INVALID_ARGUMENT;
        if (Text(operation) == "artest.component.validate.v1")
            return ARTEST_STATUS_OK;
        if (Text(operation) != "artest.command.execute.v1")
            return ARTEST_STATUS_OPERATION_NOT_SUPPORTED;
        if (Cancelled(cancellation))
            return ARTEST_STATUS_CANCELLED;
        ServiceLease service{extension->host};
        auto status = extension->host.resolve_service(extension->host.host_context,
                                                      View(component->kind == 2 ? Can : Power),
                                                      View(instrument), &service.handle, error);
        if (status != ARTEST_STATUS_OK)
            return status;
        const auto call = [&](const std::string &action, const nlohmann::json &arguments) {
            const auto text = arguments.dump();
            const auto value = Payload(text);
            return extension->host.invoke_service(extension->host.host_context, service.handle,
                                                  View(action), &value, cancellation, nullptr,
                                                  error);
        };
        if (component->kind == 2)
            status = call("artest.instrument.can.v1/send", parameters);
        else
        {
            if (component->kind == 0)
            {
                status = call("artest.instrument.power-supply.v1/set-voltage", parameters);
                if (status != ARTEST_STATUS_OK)
                    return status;
                status = call("artest.instrument.power-supply.v1/set-current-limit", parameters);
                if (status != ARTEST_STATUS_OK)
                    return status;
            }
            status = call(component->kind == 0 ? "artest.instrument.power-supply.v1/turn-on"
                                               : "artest.instrument.power-supply.v1/turn-off",
                          parameters);
        }
        if (status != ARTEST_STATUS_OK)
            return status;
        return Write(sink, {{"message", Names[component->kind] + " completed."}}, error);
    });
}
} // namespace
extern "C" ARTEST_ABI_EXPORT ARTestStatus ARTEST_ABI_CALL ARTestExtension_Query(
    uint32_t major, uint32_t minor, ARTestExtensionApiV0 *out, ARTestErrorBuffer *)
{
    if (!out || out->struct_size < sizeof(*out))
        return ARTEST_STATUS_INVALID_ARGUMENT;
    if (major != ARTEST_EXTENSION_ABI_MAJOR || minor > ARTEST_EXTENSION_ABI_MINOR)
        return ARTEST_STATUS_INCOMPATIBLE_ABI;
    *out = {sizeof(*out), major,  minor,     0,     View(Id), View(Version), &Create,
            &Destroy,     &Count, &Describe, &Make, &Drop,    &Invoke};
    return ARTEST_STATUS_OK;
}

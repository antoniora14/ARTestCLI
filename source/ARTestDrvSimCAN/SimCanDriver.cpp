#define ARTEST_EXTENSION_EXPORTS

#include "../ExtensionSupport/NativeSupport.h"
#include <memory>
#include <vector>

using namespace artest::native;


namespace
{
const std::string Id        = "com.artest.extension.sim-can";
const std::string Type      = "com.artest.driver.sim.can";
const std::string Version   = "0.1.0";
const std::string Contract  = "artest.contract.instrument.can.v1";
const std::string Name      = "Simulated CAN";
} // namespace

struct ARTestExtensionOpaque
{
    ARTestHostApiV0 host{};
};
struct ARTestComponentOpaque
{
    bool initialized = false;
    bool failInitialize = false;
    bool failShutdown = false;
    std::string resource;
    std::vector<nlohmann::json> messages;
};

namespace
{
ARTestStatus ARTEST_ABI_CALL Create(const ARTestHostApiV0 *host, const ARTestPayloadView *, ARTestExtensionHandle *out, ARTestErrorBuffer *error) noexcept
{
    if (!host || !out || host->struct_size < sizeof(*host) || !host->log)
        return ARTEST_STATUS_INVALID_ARGUMENT;

    return Guard(error, [&]() -> ARTestStatus
    {
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
    return 1;
}

ARTestStatus ARTEST_ABI_CALL Describe(ARTestExtensionHandle, std::size_t index, ARTestComponentDescriptorV0 *out, ARTestErrorBuffer *) noexcept
{
    if (index || !out || out->struct_size < sizeof(*out)) return ARTEST_STATUS_INVALID_ARGUMENT;

    *out = {sizeof(*out),
            ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER,
            ARTEST_COMPONENT_FLAG_SIMULATED,
            View(Type),
            View(Contract),
            View(Version),
            View(Name),
            {}};

    return ARTEST_STATUS_OK;
}

ARTestStatus ARTEST_ABI_CALL Make(ARTestExtensionHandle, ARTestStringView type, const ARTestPayloadView *configuration, ARTestComponentHandle *out, ARTestErrorBuffer *error) noexcept
{
    if (!out) return ARTEST_STATUS_INVALID_ARGUMENT;

    return Guard(error, [&]() -> ARTestStatus
    {
        if (Text(type) != Type) return ARTEST_STATUS_NOT_FOUND;

        const auto json = Parse(configuration);
        auto value = std::make_unique<ARTestComponentOpaque>();
        value->resource = json.value("hw-rsrc", std::string{});
        value->failInitialize = json.value("failInitialize", false);
        value->failShutdown = json.value("failShutdown", false);
        *out = value.release();

        return ARTEST_STATUS_OK;
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
    if (!extension || !component) return ARTEST_STATUS_INVALID_ARGUMENT;

    return Guard(error, [&]() -> ARTestStatus
    {
        const auto action = Text(operation);

        if (action == "artest.lifecycle.shutdown.v1")
        {
            component->initialized = false;
            component->messages.clear();
            if (component->failShutdown)
            {
                Error(error, "Simulated CAN cleanup failure.");
                return ARTEST_STATUS_EXTENSION_FAILURE;
            }
            return ARTEST_STATUS_OK;
        }
        if (Cancelled(cancellation)) return ARTEST_STATUS_CANCELLED;
        if (action == "artest.lifecycle.initialize.v1")
        {
            if (component->resource.empty() || component->failInitialize)
            {
                Error(error, "[CAN_RESOURCE_MISSING] CAN resource is missing or unavailable.");
                return ARTEST_STATUS_RESOURCE_UNAVAILABLE;
            }
            component->initialized = true;
            return ARTEST_STATUS_OK;
        }
        if (!component->initialized) return ARTEST_STATUS_INVALID_STATE;
        if (action != "artest.instrument.can.v1/send") return ARTEST_STATUS_OPERATION_NOT_SUPPORTED;

        const auto message = Parse(payload);
        const auto dlc = message.at("dlc").get<int>();
        const auto &bytes = message.at("data");
        const auto idText = message.at("id").get<std::string>();
        std::size_t consumed = 0;
        const auto id = std::stoul(idText, &consumed, 0);

        if (consumed != idText.size() || id > 0x1fffffffUL || dlc < 0 || dlc > 8 || !bytes.is_array() || bytes.size() != static_cast<std::size_t>(dlc))
            return ARTEST_STATUS_INVALID_ARGUMENT;

        for (const auto &byte : bytes)
            if (!byte.is_number_integer() || byte < 0 || byte > 255)
                return ARTEST_STATUS_INVALID_ARGUMENT;

        component->messages.push_back(message);
        return Write(sink, {{"sent", true}, {"messageCount", component->messages.size()}}, error);
    });
}
} // namespace

extern "C" ARTEST_ABI_EXPORT ARTestStatus ARTEST_ABI_CALL ARTestExtension_Query(uint32_t major, uint32_t minor, ARTestExtensionApiV0 *out, ARTestErrorBuffer *)
{
    if (!out || out->struct_size < sizeof(*out)) return ARTEST_STATUS_INVALID_ARGUMENT;
    if (major != ARTEST_EXTENSION_ABI_MAJOR || minor > ARTEST_EXTENSION_ABI_MINOR) return ARTEST_STATUS_INCOMPATIBLE_ABI;

    *out = {sizeof(*out), major, minor, 0, View(Id), View(Version), &Create, &Destroy, &Count, &Describe, &Make, &Drop, &Invoke};

    return ARTEST_STATUS_OK;
}

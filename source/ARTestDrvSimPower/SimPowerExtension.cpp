#define ARTEST_EXTENSION_EXPORTS
#include "../ARTest.SDK/include/ARTestExtensionAbi.h"
#include "../ThirdParty/json.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>

namespace
{
    const std::string ExtensionId = "com.artest.extension.sim-power";
    const std::string ExtensionVersion = "0.1.0";
    const std::string TypeId = "com.artest.driver.sim.power";
    const std::string ContractId = "artest.contract.instrument.power-supply.v1";
    const std::string DisplayName = "ARTest Simulated Power Supply";
    const std::string Empty;

    [[nodiscard]] ARTestStringView View(const std::string& value) noexcept
    {
        return {value.data(), value.size()};
    }

    [[nodiscard]] std::string ToString(ARTestStringView value)
    {
        return value.data == nullptr ? std::string{} : std::string{value.data, value.size};
    }

    void SetError(ARTestErrorBuffer* error, const std::string& message) noexcept
    {
        if (error == nullptr) return;
        error->required_size = message.size() + 1U;
        if (error->data == nullptr || error->capacity == 0U) return;
        const auto count = (std::min)(message.size(), error->capacity - 1U);
        std::copy_n(message.data(), count, error->data);
        error->data[count] = '\0';
    }

    [[nodiscard]] nlohmann::json Parse(const ARTestPayloadView* payload)
    {
        if (payload == nullptr || payload->encoding != ARTEST_PAYLOAD_ENCODING_JSON_UTF8)
            return nlohmann::json::object();
        return nlohmann::json::parse(
            reinterpret_cast<const char*>(payload->bytes.data),
            reinterpret_cast<const char*>(payload->bytes.data) + payload->bytes.size);
    }

    ARTestStatus Write(
        const ARTestResultSinkV0* sink,
        const nlohmann::json& value,
        ARTestErrorBuffer* error)
    {
        if (sink == nullptr || sink->write == nullptr) return ARTEST_STATUS_OK;
        const auto text = value.dump();
        const std::string schema = "artest.schema.instrument.power-supply.result.v1";
        const std::string media = "application/json; charset=utf-8";
        const ARTestPayloadView payload{
            sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema), View(media),
            {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}};
        return sink->write(sink->sink_context, &payload, error);
    }
}

struct ARTestExtensionOpaque
{
    ARTestHostApiV0 host{};
};

struct ARTestComponentOpaque
{
    bool initialized = false;
    bool failShutdown = false;
    std::map<int, double> voltages;
    std::map<int, bool> outputs;
};

namespace
{
    ARTestStatus ARTEST_ABI_CALL CreateExtension(
        const ARTestHostApiV0* host,
        const ARTestPayloadView*,
        ARTestExtensionHandle* output,
        ARTestErrorBuffer* error) noexcept
    {
        if (host == nullptr || output == nullptr
            || host->struct_size < sizeof(ARTestHostApiV0))
        {
            SetError(error, "A compatible host API and output handle are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            auto extension = std::make_unique<ARTestExtensionOpaque>();
            extension->host = *host;
            *output = extension.release();
            return ARTEST_STATUS_OK;
        }
        catch (...)
        {
            SetError(error, "The simulated power extension could not be created.");
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
    }

    void ARTEST_ABI_CALL DestroyExtension(ARTestExtensionHandle extension) noexcept
    {
        delete extension;
    }

    std::size_t ARTEST_ABI_CALL GetCount(ARTestExtensionHandle) noexcept
    {
        return 1U;
    }

    ARTestStatus ARTEST_ABI_CALL GetDescriptor(
        ARTestExtensionHandle,
        std::size_t index,
        ARTestComponentDescriptorV0* descriptor,
        ARTestErrorBuffer* error) noexcept
    {
        if (index != 0U || descriptor == nullptr
            || descriptor->struct_size < sizeof(ARTestComponentDescriptorV0))
        {
            SetError(error, "The driver descriptor index or output is invalid.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        *descriptor = {
            sizeof(ARTestComponentDescriptorV0),
            ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER,
            ARTEST_COMPONENT_FLAG_SIMULATED,
            View(TypeId), View(ContractId), View(ExtensionVersion),
            View(DisplayName),
            {sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_UNSPECIFIED,
                View(Empty), View(Empty), {nullptr, 0U}}};
        return ARTEST_STATUS_OK;
    }

    ARTestStatus ARTEST_ABI_CALL CreateComponent(
        ARTestExtensionHandle,
        ARTestStringView typeId,
        const ARTestPayloadView* configuration,
        ARTestComponentHandle* output,
        ARTestErrorBuffer* error) noexcept
    {
        if (output == nullptr || ToString(typeId) != TypeId)
        {
            SetError(error, "The simulated power driver type is invalid.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            auto component = std::make_unique<ARTestComponentOpaque>();
            const auto json = Parse(configuration);
            component->failShutdown = json.value("failShutdown", false);
            *output = component.release();
            return ARTEST_STATUS_OK;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        catch (...)
        {
            SetError(error, "The driver component could not be created.");
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
    }

    void ARTEST_ABI_CALL DestroyComponent(
        ARTestExtensionHandle, ARTestComponentHandle component) noexcept
    {
        delete component;
    }

    ARTestStatus ARTEST_ABI_CALL Invoke(
        ARTestExtensionHandle extension,
        ARTestComponentHandle component,
        ARTestStringView operation,
        const ARTestPayloadView* request,
        const ARTestInvocationContextV0* invocation,
        const ARTestResultSinkV0* sink,
        ARTestErrorBuffer* error) noexcept
    {
        if (extension == nullptr || component == nullptr)
            return ARTEST_STATUS_INVALID_ARGUMENT;
        try
        {
            if (invocation != nullptr && invocation->is_cancellation_requested != nullptr
                && invocation->is_cancellation_requested(
                    invocation->cancellation_context) == ARTEST_TRUE)
            {
                SetError(error, "The simulated driver invocation was cancelled.");
                return ARTEST_STATUS_CANCELLED;
            }
            const auto operationId = ToString(operation);
            const auto json = Parse(request);
            if (operationId == "artest.lifecycle.initialize.v1")
            {
                component->initialized = true;
                extension->host.log(
                    extension->host.host_context, ARTEST_LOG_INFORMATION,
                    View(TypeId), View(std::string{"Simulated power driver initialized."}));
                return ARTEST_STATUS_OK;
            }
            if (operationId == "artest.lifecycle.shutdown.v1")
            {
                component->initialized = false;
                component->outputs.clear();
                extension->host.log(
                    extension->host.host_context, ARTEST_LOG_INFORMATION,
                    View(TypeId), View(std::string{"Simulated power driver shut down."}));
                if (component->failShutdown)
                {
                    SetError(error, "Simulated shutdown failure was requested.");
                    return ARTEST_STATUS_EXTENSION_FAILURE;
                }
                return ARTEST_STATUS_OK;
            }
            if (!component->initialized)
            {
                SetError(error, "The simulated power driver is not initialized.");
                return ARTEST_STATUS_INVALID_STATE;
            }
            const auto channel = json.value("channel", -1);
            if (channel < 0)
            {
                SetError(error, "channel must be zero or greater.");
                return ARTEST_STATUS_INVALID_ARGUMENT;
            }
            if (operationId == "artest.instrument.power-supply.v1/set-voltage")
            {
                const auto voltage = json.value("voltage", -1.0);
                if (voltage < 0.0)
                {
                    SetError(error, "voltage must be zero or greater.");
                    return ARTEST_STATUS_INVALID_ARGUMENT;
                }
                component->voltages[channel] = voltage;
                return ARTEST_STATUS_OK;
            }
            if (operationId == "artest.instrument.power-supply.v1/turn-on")
            {
                component->outputs[channel] = true;
                return ARTEST_STATUS_OK;
            }
            if (operationId == "artest.instrument.power-supply.v1/turn-off")
            {
                component->outputs[channel] = false;
                return ARTEST_STATUS_OK;
            }
            if (operationId == "artest.instrument.power-supply.v1/read-state")
                return Write(sink, {
                    {"channel", channel},
                    {"voltage", component->voltages[channel]},
                    {"outputOn", component->outputs[channel]}}, error);
            SetError(error, "The requested driver operation is not supported.");
            return ARTEST_STATUS_OPERATION_NOT_SUPPORTED;
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown simulated driver failure.");
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
    }
}

extern "C" ARTEST_ABI_EXPORT ARTestStatus ARTEST_ABI_CALL
    ARTestExtension_Query(
        std::uint32_t requestedMajor,
        std::uint32_t requestedMinor,
        ARTestExtensionApiV0* api,
        ARTestErrorBuffer* error)
{
    if (api == nullptr || api->struct_size < sizeof(ARTestExtensionApiV0))
        return ARTEST_STATUS_INVALID_ARGUMENT;
    if (requestedMajor != ARTEST_EXTENSION_ABI_MAJOR
        || requestedMinor > ARTEST_EXTENSION_ABI_MINOR)
    {
        SetError(error, "The requested extension ABI is incompatible.");
        return ARTEST_STATUS_INCOMPATIBLE_ABI;
    }
    *api = {
        sizeof(ARTestExtensionApiV0),
        ARTEST_EXTENSION_ABI_MAJOR, ARTEST_EXTENSION_ABI_MINOR, 0U,
        View(ExtensionId), View(ExtensionVersion),
        &CreateExtension, &DestroyExtension, &GetCount, &GetDescriptor,
        &CreateComponent, &DestroyComponent, &Invoke};
    return ARTEST_STATUS_OK;
}

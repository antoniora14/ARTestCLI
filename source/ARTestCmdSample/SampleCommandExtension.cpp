#define ARTEST_EXTENSION_EXPORTS
#include "../ARTest.SDK/include/ARTestExtensionAbi.h"
#include "../ThirdParty/json.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace
{
    const std::string ExtensionId 		= "com.artest.extension.sample-command";
    const std::string ExtensionVersion 	= "0.1.0";
    const std::string TypeId 			= "com.artest.command.sample.power-cycle";
    const std::string ContractId 		= "artest.contract.command.v1";
    const std::string DriverContract 	= "artest.contract.instrument.power-supply.v1";
    const std::string DisplayName 		= "ARTest Sample Power Cycle";
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
    [[nodiscard]] ARTestPayloadView Payload(const std::string& text) noexcept
    {
        static const std::string schema = "artest.schema.generic-json.v1";
        static const std::string media = "application/json; charset=utf-8";
        return {
            sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema), View(media),
            {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}};
    }
    ARTestStatus Write(const ARTestResultSinkV0* sink, const nlohmann::json& value, ARTestErrorBuffer* error)
    {
        if (sink == nullptr || sink->write == nullptr) return ARTEST_STATUS_OK;
        const auto text = value.dump();
        const auto payload = Payload(text);
        return sink->write(sink->sink_context, &payload, error);
    }
    [[nodiscard]] bool Cancelled(const ARTestInvocationContextV0* invocation) noexcept
    {
        return invocation != nullptr
            && invocation->is_cancellation_requested != nullptr
            && invocation->is_cancellation_requested(
                invocation->cancellation_context) == ARTEST_TRUE;
    }
}

struct ARTestExtensionOpaque { ARTestHostApiV0 host{}; };
struct ARTestComponentOpaque {};

namespace
{
    ARTestStatus ARTEST_ABI_CALL CreateExtension(const ARTestHostApiV0* host, const ARTestPayloadView*, ARTestExtensionHandle* output, ARTestErrorBuffer* error) noexcept
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
            SetError(error, "The sample command extension could not be created.");
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
    }

	void ARTEST_ABI_CALL DestroyExtension(ARTestExtensionHandle value) noexcept
    {
        delete value;
    }

	std::size_t ARTEST_ABI_CALL GetCount(ARTestExtensionHandle) noexcept
    {
        return 1U;
    }

	ARTestStatus ARTEST_ABI_CALL GetDescriptor(ARTestExtensionHandle, std::size_t index, ARTestComponentDescriptorV0* descriptor, ARTestErrorBuffer* error) noexcept
    {
        if (index != 0U || descriptor == nullptr
            || descriptor->struct_size < sizeof(ARTestComponentDescriptorV0))
        {
            SetError(error, "The command descriptor index or output is invalid.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        *descriptor = {
            sizeof(ARTestComponentDescriptorV0),
            ARTEST_COMPONENT_KIND_COMMAND, ARTEST_COMPONENT_FLAG_NONE,
            View(TypeId), View(ContractId), View(ExtensionVersion), View(DisplayName),
            {sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_UNSPECIFIED,
                View(Empty), View(Empty), {nullptr, 0U}}};
        return ARTEST_STATUS_OK;
    }

	ARTestStatus ARTEST_ABI_CALL CreateComponent(ARTestExtensionHandle, ARTestStringView typeId, const ARTestPayloadView*, ARTestComponentHandle* output, ARTestErrorBuffer* error) noexcept
    {
        if (output == nullptr || ToString(typeId) != TypeId)
        {
            SetError(error, "The sample command type is invalid.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        try
        {
            *output = new ARTestComponentOpaque();
            return ARTEST_STATUS_OK;
        }
        catch (...)
        {
            SetError(error, "The command component could not be created.");
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
    }

	void ARTEST_ABI_CALL DestroyComponent(ARTestExtensionHandle, ARTestComponentHandle value) noexcept
    {
        delete value;
    }

    ARTestStatus ValidateRequest(const nlohmann::json& request, ARTestErrorBuffer* error)
    {
        if (!request.is_object()
            || !request.contains("instrumentId")
            || !request["instrumentId"].is_string()
            || request["instrumentId"].get<std::string>().empty()
            || !request.contains("parameters")
            || !request["parameters"].is_object())
        {
            SetError(error, "instrumentId and parameters are required.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        const auto& parameters = request["parameters"];
        const auto channel = parameters.value("channel", -1);
        const auto voltage = parameters.value("voltage", -1.0);
        const auto holdMs = parameters.value("holdMs", 0);
        if (channel < 0 || voltage < 0.0 || holdMs < 0 || holdMs > 60000)
        {
            SetError(
                error,
                "channel, voltage, and holdMs must be within their valid ranges.");
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        return ARTEST_STATUS_OK;
    }

    ARTestStatus InvokeDriver(
        ARTestExtensionOpaque& extension,
        ARTestServiceHandle service,
        const std::string& operation,
        const nlohmann::json& request,
        const ARTestInvocationContextV0* invocation,
        ARTestErrorBuffer* error)
    {
        const auto text = request.dump();
        const auto payload = Payload(text);
        return extension.host.invoke_service(
            extension.host.host_context, service, View(operation),
            &payload, invocation, nullptr, error);
    }

    ARTestStatus ARTEST_ABI_CALL Invoke(
        ARTestExtensionHandle extension, ARTestComponentHandle component,
        ARTestStringView operation, const ARTestPayloadView* requestPayload,
        const ARTestInvocationContextV0* invocation,
        const ARTestResultSinkV0* sink, ARTestErrorBuffer* error) noexcept
    {
        if (extension == nullptr || component == nullptr)
            return ARTEST_STATUS_INVALID_ARGUMENT;
        try
        {
            const auto operationId = ToString(operation);
            const auto request = Parse(requestPayload);
            const auto validation = ValidateRequest(request, error);
            if (operationId == "artest.component.validate.v1")
                return validation;
            if (operationId != "artest.command.execute.v1")
            {
                SetError(error, "The requested command operation is not supported.");
                return ARTEST_STATUS_OPERATION_NOT_SUPPORTED;
            }
            if (validation != ARTEST_STATUS_OK) return validation;
            if (Cancelled(invocation))
            {
                SetError(error, "The sample command was cancelled.");
                return ARTEST_STATUS_CANCELLED;
            }

            const auto instanceId = request["instrumentId"].get<std::string>();
            ARTestServiceHandle service = nullptr;
            auto status = extension->host.resolve_service(
                extension->host.host_context, View(DriverContract),
                View(instanceId), &service, error);
            if (status != ARTEST_STATUS_OK) return status;
            struct ServiceGuard
            {
                ARTestExtensionOpaque& extension;
                ARTestServiceHandle service;
                ~ServiceGuard()
                {
                    extension.host.release_service(
                        extension.host.host_context, service);
                }
            } guard{*extension, service};

            const auto& parameters = request["parameters"];
            const auto channel = parameters.value("channel", 0);
            status = InvokeDriver(
                *extension, service,
                "artest.instrument.power-supply.v1/set-voltage",
                {{"channel", channel}, {"voltage", parameters.value("voltage", 0.0)}},
                invocation, error);
            if (status != ARTEST_STATUS_OK) return status;
            status = InvokeDriver(
                *extension, service,
                "artest.instrument.power-supply.v1/turn-on",
                {{"channel", channel}}, invocation, error);
            if (status != ARTEST_STATUS_OK) return status;

            const auto holdMs = parameters.value("holdMs", 0);
            for (int elapsed = 0; elapsed < holdMs; elapsed += 10)
            {
                if (Cancelled(invocation))
                {
                    static_cast<void>(InvokeDriver(
                        *extension, service,
                        "artest.instrument.power-supply.v1/turn-off",
                        {{"channel", channel}}, invocation, error));
                    SetError(error, "The sample command was cancelled.");
                    return ARTEST_STATUS_CANCELLED;
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds{(std::min)(10, holdMs - elapsed)});
            }
            status = InvokeDriver(
                *extension, service,
                "artest.instrument.power-supply.v1/turn-off",
                {{"channel", channel}}, invocation, error);
            if (status != ARTEST_STATUS_OK) return status;
            extension->host.log(
                extension->host.host_context, ARTEST_LOG_INFORMATION,
                View(TypeId), View(std::string{"Sample command completed through driver service."}));
            return Write(sink, {
                {"message", "Native command invoked the simulated driver successfully."},
                {"instrumentId", instanceId},
                {"channel", channel}}, error);
        }
        catch (const std::exception& exception)
        {
            SetError(error, exception.what());
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
        catch (...)
        {
            SetError(error, "Unknown sample command failure.");
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
    }
}

extern "C" ARTEST_ABI_EXPORT ARTestStatus ARTEST_ABI_CALL ARTestExtension_Query(std::uint32_t requestedMajor, std::uint32_t requestedMinor, ARTestExtensionApiV0* api, ARTestErrorBuffer* error)
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
        ARTEST_EXTENSION_ABI_MAJOR,
		ARTEST_EXTENSION_ABI_MINOR,
		0U,
        View(ExtensionId),
		View(ExtensionVersion),
        &CreateExtension,
		&DestroyExtension,
		&GetCount,
		&GetDescriptor,
        &CreateComponent,
		&DestroyComponent,
		&Invoke
		};

    return ARTEST_STATUS_OK;
}

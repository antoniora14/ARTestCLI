#pragma once

#include "ARTestEngineApi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <string>
#include <string_view>

namespace artest::sdk
{
    struct ClientStatus
    {
        ARTestStatus code = ARTEST_STATUS_OK;
        std::string message;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return code == ARTEST_STATUS_OK;
        }
    };

    enum class ExecutionDecision
    {
        Continue,
        Cancel
    };

    struct StepExecutionInfo
    {
        std::size_t commandIndex = 0U;
        std::uint64_t stepId = 0U;
        std::string commandName;
    };

    using BeforeStepCallback = std::function<ExecutionDecision(const StepExecutionInfo&)>;

    class EngineClient final
    {
    public:
        EngineClient() = default;
        ~EngineClient() { Reset(); }
        EngineClient(const EngineClient&) = delete;
        EngineClient& operator=(const EngineClient&) = delete;

        [[nodiscard]] ClientStatus Create(std::string configuration = "{}")
        {
            Reset();
            m_api.struct_size = sizeof(m_api);

            if (auto status = InvokeQuery(); !status.Succeeded()) return status;
            const auto payload = Payload(configuration);

            Error error;
            const auto code = m_api.create_engine(&payload, &m_engine, &error.value);

            return {code, error.Message()};
        }

        [[nodiscard]] ClientStatus RefreshCatalog(const std::string& approvedRoot)
        {
            if (m_engine == nullptr) return InvalidState("The engine was not created.");

            Error error;
            const ARTestStringView root{approvedRoot.data(), approvedRoot.size()};
            const auto code = m_api.refresh_catalog(m_engine, root, &error.value);

            return {code, error.Message()};
        }

        [[nodiscard]] ClientStatus SubscribeEvents(std::function<void(std::string_view)> callback)
        {
            if (m_engine == nullptr) return InvalidState("The engine was not created.");

            if (!callback) return { ARTEST_STATUS_INVALID_ARGUMENT, "An event callback is required." };
            UnsubscribeEvents();
            m_eventCallback = std::move(callback);

            Error error;
            const auto code = m_api.subscribe_events(m_engine, &DispatchEvent, this, &m_subscription, &error.value);

            if (code != ARTEST_STATUS_OK) m_eventCallback = {};
            return {code, error.Message()};
        }

        [[nodiscard]] ClientStatus Compile(const std::string& testPlanJson)
        {
            DestroyExecutionObjects();

            if (m_engine == nullptr) return InvalidState("The engine was not created.");
            const auto payload = Payload(testPlanJson);

            Error error;
            const auto code = m_api.compile_plan(m_engine, &payload, &m_plan, &error.value);

            return {code, error.Message()};
        }

        [[nodiscard]] ClientStatus CompileDetailed(const std::string& testPlanJson, std::string& reportJson)
        {
            DestroyExecutionObjects();
            reportJson.clear();

            if (m_engine == nullptr) return InvalidState("The engine was not created.");
            if (m_api.compile_plan_detailed == nullptr)
                return {ARTEST_STATUS_OPERATION_NOT_SUPPORTED,
                    "Detailed compilation requires ARTestEngine API 0.2 or newer."};

            const auto payload = Payload(testPlanJson);
            ARTestResultSinkV0 sink{
                sizeof(ARTestResultSinkV0), 0U, &reportJson, &WriteString};
            Error error;
            const auto code = m_api.compile_plan_detailed(
                m_engine, &payload, &m_plan, &sink, &error.value);
            return {code, error.Message()};
        }

        [[nodiscard]] ClientStatus Start()
        {
            if (m_engine == nullptr || m_plan == nullptr) return InvalidState("A compiled plan is required.");
            if (m_session != nullptr) return InvalidState("A session has already been started.");

            Error error;
            const auto code = m_api.start_session(m_engine, m_plan, &m_session, &error.value);

            return {code, error.Message()};
        }

        [[nodiscard]] ClientStatus Start(BeforeStepCallback callback)
        {
            if (m_engine == nullptr || m_plan == nullptr)
                return InvalidState("A compiled plan is required.");
            if (m_session != nullptr)
                return InvalidState("A session has already been started.");
            if (!callback)
                return {ARTEST_STATUS_INVALID_ARGUMENT,
                    "A before-step callback is required."};
            if (m_api.start_session_controlled == nullptr)
                return {ARTEST_STATUS_OPERATION_NOT_SUPPORTED,
                    "Controlled sessions require ARTestEngine API 0.2 or newer."};

            m_beforeStepCallback = std::move(callback);
            const ARTestSessionOptionsV0 options{
                sizeof(ARTestSessionOptionsV0),
                0U,
                &DispatchBeforeStep,
                this};
            Error error;
            const auto code = m_api.start_session_controlled(
                m_engine, m_plan, &options, &m_session, &error.value);
            if (code != ARTEST_STATUS_OK) m_beforeStepCallback = {};
            return {code, error.Message()};
        }

        [[nodiscard]] ClientStatus Cancel()
        {
            if (m_session == nullptr) return InvalidState("An active session is required.");

            Error error;
            const auto code = m_api.cancel_session(m_session, &error.value);

            return {code, error.Message()};
        }

        void RequestCancel() noexcept
        {
            if (m_session == nullptr) return;
            Error error;
            static_cast<void>(m_api.cancel_session(m_session, &error.value));
        }

        [[nodiscard]] ClientStatus Wait(std::uint32_t hostTimeoutMs, bool& completed)
        {
            completed = false;
            if (m_session == nullptr) return InvalidState("An active session is required.");
            Error error;
            ARTestBool32 nativeCompleted = ARTEST_FALSE;
            const auto code = m_api.wait_session(m_session, hostTimeoutMs, &nativeCompleted, &error.value);
            completed = nativeCompleted == ARTEST_TRUE;
            return {code, error.Message()};
        }

        [[nodiscard]] ClientStatus SerializeResult(std::string& json)
        {
            json.clear();
            if (m_session == nullptr) return InvalidState("A completed session is required.");

            Error error;
            if (m_result == nullptr)
            {
                const auto code = m_api.get_session_result(m_session, &m_result, &error.value);
                if (code != ARTEST_STATUS_OK) return {code, error.Message()};
            }
            ARTestResultSinkV0 sink{ sizeof(ARTestResultSinkV0), 0U, &json, &WriteString };
            const auto code = m_api.serialize_result(m_result, &sink, &error.value);

            return {code, error.Message()};
        }

    private:
        struct Error
        {
            char text[2048]{};
            ARTestErrorBuffer value{sizeof(ARTestErrorBuffer), 0U, text, sizeof(text), 0U};
            [[nodiscard]] std::string Message() const { return text; }
        };

        [[nodiscard]] ClientStatus InvokeQuery()
        {
            Error error;
            const auto code = ARTestEngine_QueryApi(ARTEST_ENGINE_API_MAJOR, ARTEST_ENGINE_API_MINOR, &m_api, &error.value);
            return {code, error.Message()};
        }

        [[nodiscard]] static ARTestPayloadView Payload(const std::string& value) noexcept
        {
            static const std::string schema = "artest.schema.generic-json.v1";
            static const std::string media = "application/json; charset=utf-8";
            return {
                sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
                {schema.data(), schema.size()}, {media.data(), media.size()},
                {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}};
        }

        static ARTestStatus ARTEST_ABI_CALL WriteString(void* context, const ARTestPayloadView* payload, ARTestErrorBuffer*) noexcept
        {
            if (context == nullptr || payload == nullptr) return ARTEST_STATUS_INVALID_ARGUMENT;

            try
            {
                static_cast<std::string*>(context)->assign(
                    reinterpret_cast<const char*>(payload->bytes.data),
                    payload->bytes.size);
                return ARTEST_STATUS_OK;
            }
            catch (...)
            {
                return ARTEST_STATUS_HOST_FAILURE;
            }
        }

        static void ARTEST_ABI_CALL DispatchEvent(void* context, const ARTestPayloadView* payload) noexcept
        {
            if (context == nullptr || payload == nullptr) return;

            try
            {
                auto& self = *static_cast<EngineClient*>(context);
                if (self.m_eventCallback)
                    self.m_eventCallback(std::string_view{
                        reinterpret_cast<const char*>(payload->bytes.data),
                        payload->bytes.size});
            }
            catch (...) {}
        }

        static ARTestStatus ARTEST_ABI_CALL DispatchBeforeStep(
            void* context,
            const ARTestStepExecutionInfoV0* info,
            ARTestExecutionDecision* decision,
            ARTestErrorBuffer* error) noexcept
        {
            if (context == nullptr || info == nullptr || decision == nullptr
                || info->struct_size < sizeof(ARTestStepExecutionInfoV0)
                || (info->command_name.data == nullptr
                    && info->command_name.size != 0U)
                || info->command_index
                    > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
            {
                SetCallbackError(error, "Invalid before-step callback arguments.");
                return ARTEST_STATUS_INVALID_ARGUMENT;
            }
            try
            {
                auto& self = *static_cast<EngineClient*>(context);
                if (!self.m_beforeStepCallback)
                {
                    SetCallbackError(error, "The before-step callback is no longer available.");
                    return ARTEST_STATUS_INVALID_STATE;
                }
                const auto commandName = info->command_name.data == nullptr
                    ? std::string{}
                    : std::string{
                        info->command_name.data,
                        info->command_name.size};
                const StepExecutionInfo value{
                    static_cast<std::size_t>(info->command_index),
                    info->step_id,
                    commandName};
                *decision = self.m_beforeStepCallback(value) == ExecutionDecision::Continue
                    ? ARTEST_EXECUTION_CONTINUE
                    : ARTEST_EXECUTION_CANCEL;
                return ARTEST_STATUS_OK;
            }
            catch (const std::exception& exception)
            {
                SetCallbackError(error, exception.what());
                return ARTEST_STATUS_HOST_FAILURE;
            }
            catch (...)
            {
                SetCallbackError(error, "The before-step callback raised an unknown failure.");
                return ARTEST_STATUS_HOST_FAILURE;
            }
        }

        static void SetCallbackError(ARTestErrorBuffer* error, std::string_view message) noexcept
        {
            if (error == nullptr) return;
            error->required_size = message.size() + 1U;
            if (error->data == nullptr || error->capacity == 0U) return;
            const auto count = (std::min)(message.size(), error->capacity - 1U);
            std::copy_n(message.data(), count, error->data);
            error->data[count] = '\0';
        }

        [[nodiscard]] static ClientStatus InvalidState(std::string message)
        {
            return {ARTEST_STATUS_INVALID_STATE, std::move(message)};
        }

        void DestroyExecutionObjects() noexcept
        {
            if (m_result != nullptr)
            {
                m_api.destroy_result(m_result);
                m_result = nullptr;
            }
            if (m_session != nullptr)
            {
                m_api.destroy_session(m_session);
                m_session = nullptr;
            }
            m_beforeStepCallback = {};
            if (m_plan != nullptr)
            {
                m_api.destroy_compiled_plan(m_plan);
                m_plan = nullptr;
            }
        }

        void Reset() noexcept
        {
            DestroyExecutionObjects();
            if (m_engine != nullptr)
            {
                UnsubscribeEvents();
                m_api.destroy_engine(m_engine);
                m_engine = nullptr;
            }
            m_api = {};
        }

        void UnsubscribeEvents() noexcept
        {
            if (m_subscription != nullptr && m_engine != nullptr)
            {
                m_api.unsubscribe_events(m_engine, m_subscription);
                m_subscription = nullptr;
            }
            m_eventCallback = {};
        }

        ARTestEngineApiV0 m_api{};
        ARTestEngineHandle m_engine = nullptr;
        ARTestCompiledPlanHandle m_plan = nullptr;
        ARTestSessionHandle m_session = nullptr;
        ARTestResultHandle m_result = nullptr;
        ARTestSubscriptionHandle m_subscription = nullptr;
        std::function<void(std::string_view)> m_eventCallback;
        BeforeStepCallback m_beforeStepCallback;
    };
}

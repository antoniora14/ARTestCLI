#pragma once

#include "ARTestEngineApi.h"

#include <algorithm>
#include <cstdint>
#include <functional>
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

        [[nodiscard]] ClientStatus Start()
        {
            if (m_engine == nullptr || m_plan == nullptr) return InvalidState("A compiled plan is required.");

            Error error;
            const auto code = m_api.start_session(m_engine, m_plan, &m_session, &error.value);

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
    };
}

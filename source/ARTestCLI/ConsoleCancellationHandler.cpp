#include "ConsoleCancellationHandler.h"

namespace artest::cli
{
    std::atomic<void*> ConsoleCancellationHandler::s_context{nullptr};
    std::atomic<ConsoleCancellationHandler::CancelFunction>
        ConsoleCancellationHandler::s_cancel{nullptr};

    ConsoleCancellationHandler::ConsoleCancellationHandler(ExecutionSession& session) noexcept
        : m_context(&session)
    {
        s_context.store(m_context, std::memory_order_release);
        s_cancel.store(&ConsoleCancellationHandler::CancelSession, std::memory_order_release);
        m_registered = SetConsoleCtrlHandler(&ConsoleCancellationHandler::HandleControl, TRUE) != FALSE;
    }

    ConsoleCancellationHandler::ConsoleCancellationHandler(
        sdk::EngineClient& engine) noexcept
        : m_context(&engine)
    {
        s_context.store(m_context, std::memory_order_release);
        s_cancel.store(&ConsoleCancellationHandler::CancelEngine, std::memory_order_release);
        m_registered = SetConsoleCtrlHandler(&ConsoleCancellationHandler::HandleControl, TRUE) != FALSE;
    }

    ConsoleCancellationHandler::~ConsoleCancellationHandler()
    {
        void* expected = m_context;
        static_cast<void>(s_context.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_acq_rel));
        s_cancel.store(nullptr, std::memory_order_release);
        if (m_registered)
        {
            static_cast<void>(SetConsoleCtrlHandler(&ConsoleCancellationHandler::HandleControl, FALSE));
        }
    }

    BOOL WINAPI ConsoleCancellationHandler::HandleControl(DWORD controlType) noexcept
    {
        if (controlType != CTRL_C_EVENT && controlType != CTRL_BREAK_EVENT)
        {
            return FALSE;
        }

        const auto cancel = s_cancel.load(std::memory_order_acquire);
        auto* context = s_context.load(std::memory_order_acquire);
        if (cancel != nullptr && context != nullptr)
        {
            cancel(context);
            return TRUE;
        }
        return FALSE;
    }

    void ConsoleCancellationHandler::CancelSession(void* context) noexcept
    {
        static_cast<ExecutionSession*>(context)->Cancel();
    }

    void ConsoleCancellationHandler::CancelEngine(void* context) noexcept
    {
        static_cast<sdk::EngineClient*>(context)->RequestCancel();
    }
}

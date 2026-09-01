#include "ConsoleCancellationHandler.h"

namespace artest::cli
{
    std::atomic<ExecutionSession*> ConsoleCancellationHandler::s_session{nullptr};

    ConsoleCancellationHandler::ConsoleCancellationHandler(ExecutionSession& session) noexcept
        : m_session(&session)
    {
        s_session.store(m_session, std::memory_order_release);
        m_registered = SetConsoleCtrlHandler(&ConsoleCancellationHandler::HandleControl, TRUE) != FALSE;
    }

    ConsoleCancellationHandler::~ConsoleCancellationHandler()
    {
        ExecutionSession* expected = m_session;
        static_cast<void>(s_session.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_acq_rel));
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

        if (auto* session = s_session.load(std::memory_order_acquire))
        {
            session->Cancel();
            return TRUE;
        }
        return FALSE;
    }
}

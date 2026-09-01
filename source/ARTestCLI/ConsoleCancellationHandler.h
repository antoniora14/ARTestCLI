#pragma once

#include "../ARTestEngine.Core/Execution/ExecutionSession.h"

#include <Windows.h>
#include <atomic>

namespace artest::cli
{
    class ConsoleCancellationHandler final
    {
    public:
        explicit ConsoleCancellationHandler(ExecutionSession& session) noexcept;
        ~ConsoleCancellationHandler();

        ConsoleCancellationHandler(const ConsoleCancellationHandler&) = delete;
        ConsoleCancellationHandler& operator=(const ConsoleCancellationHandler&) = delete;

    private:
        static BOOL WINAPI HandleControl(DWORD controlType) noexcept;

        static std::atomic<ExecutionSession*> s_session;
        ExecutionSession* m_session;
        bool m_registered = false;
    };
}

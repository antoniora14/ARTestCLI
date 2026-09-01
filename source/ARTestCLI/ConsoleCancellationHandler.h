#pragma once

#include "../ARTestEngine.Core/Execution/ExecutionSession.h"
#include "../ARTest.SDK/include/ARTestEngineClient.h"

#include <Windows.h>
#include <atomic>

namespace artest::cli
{
    class ConsoleCancellationHandler final
    {
    public:
        explicit ConsoleCancellationHandler(ExecutionSession& session) noexcept;
        explicit ConsoleCancellationHandler(sdk::EngineClient& engine) noexcept;
        ~ConsoleCancellationHandler();

        ConsoleCancellationHandler(const ConsoleCancellationHandler&) = delete;
        ConsoleCancellationHandler& operator=(const ConsoleCancellationHandler&) = delete;

    private:
        static BOOL WINAPI HandleControl(DWORD controlType) noexcept;

        using CancelFunction = void (*)(void*) noexcept;
        static void CancelSession(void* context) noexcept;
        static void CancelEngine(void* context) noexcept;
        static std::atomic<void*> s_context;
        static std::atomic<CancelFunction> s_cancel;
        void* m_context;
        bool m_registered = false;
    };
}

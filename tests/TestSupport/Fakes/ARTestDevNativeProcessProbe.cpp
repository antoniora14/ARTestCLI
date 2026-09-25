#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <filesystem>
#include <string>
int wmain(int argc, wchar_t **argv) {
    if (argc != 4) return 2;
    if (std::wstring(argv[1]) == L"child") {
        Sleep(3000);
        std::ofstream(std::filesystem::path(argv[3])) << "late writer";
        return 0;
    }
    std::wstring command = L"\"" + std::wstring(argv[2]) + L"\" child unused \"" + argv[3] + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
    if (!CreateProcessW(argv[2], command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup, &child)) return 3;
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    return 0;
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) return 2;
    const std::filesystem::path argument(argv[1]);
    const auto folder = std::filesystem::is_regular_file(argument) ? argument.parent_path() : argument;
    const auto marker = folder / (L"editor-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    {
        std::ofstream out(marker); out << "started";
    }
    Sleep(1500);
    {
        std::ofstream out(marker, std::ios::app); out << "-still-alive";
    }
    return 0;
}

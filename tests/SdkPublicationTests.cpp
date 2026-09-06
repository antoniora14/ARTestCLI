#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <gtest/gtest.h>

namespace
{
class SdkPublicationTests : public testing::TestWithParam<const char *> {};
}
TEST_P(SdkPublicationTests, PreservesValidatedPackageBoundaries)
{
    wchar_t path[32768]{};
    ASSERT_NE(GetModuleFileNameW(nullptr, path, 32768), 0U);
    const auto binary = std::filesystem::path{path}.parent_path();
    auto root = binary;
    for (int index = 0; index < 4; ++index) root = root.parent_path();
    wchar_t system[32768]{};
    ASSERT_NE(GetSystemDirectoryW(system, 32768), 0U);
    const auto shell = std::filesystem::path{system} / L"WindowsPowerShell/v1.0/powershell.exe";
    const std::string caseName = GetParam();
    std::wstring command = L"\"" + shell.wstring() + L"\" -NoProfile -ExecutionPolicy Bypass -File \"" +
        (root / "scripts/test-sdk-publication.ps1").wstring() + L"\" -Configuration " +
        binary.filename().wstring() + L" -Case " + std::wstring(caseName.begin(), caseName.end());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    ASSERT_TRUE(CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
    const auto waited = WaitForSingleObject(process.hProcess, 60000);
    DWORD code = 1;
    if (waited != WAIT_OBJECT_0)
    {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    else GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    ASSERT_EQ(waited, WAIT_OBJECT_0) << caseName;
    EXPECT_EQ(code, 0U) << "Publication case failed: " << caseName
        << ". Run scripts/test-sdk-publication.ps1 -Case " << caseName << " for diagnostics.";
}
INSTANTIATE_TEST_SUITE_P(BuildTools, SdkPublicationTests, testing::Values(
    "FreshAndStale", "BadGenerator", "WrongDll", "InvalidSchema",
    "ReorderedDescriptors", "InvalidVerdict", "FailedPromotion",
    "InterruptedBackup", "InterruptedPromoted", "LockedWriter", "ForeignContent", "ToolTimeout"),
    [](const testing::TestParamInfo<const char *> &info) { return info.param; });

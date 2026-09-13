#pragma once
#include "../../examples/ARTestTcpHello/simulator/Server.h"
#include <atomic>
#include <thread>
#include <gtest/gtest.h>

namespace artest::tests::tcp
{
using namespace artest::tcphello;
inline std::filesystem::path Binaries()
{
    wchar_t buffer[32768]{};
    if (!GetModuleFileNameW(nullptr, buffer, 32768)) throw std::runtime_error("Cannot locate test executable.");
    return std::filesystem::path(buffer).parent_path();
}
inline std::filesystem::path Repository()
{
    auto root = Binaries();
    for (int i = 0; i < 4; ++i) root = root.parent_path();
    return root;
}
class ServerProcess final
{
    HANDLE process_ = nullptr, job_ = nullptr, stop_ = nullptr;
    std::filesystem::path root_;
    unsigned short port_ = 0;
    bool cleanExit_ = false;
    void Stop() noexcept
    {
        if (stop_) SetEvent(stop_);
        if (process_ && WaitForSingleObject(process_, 3000) != WAIT_OBJECT_0)
        {
            TerminateJobObject(job_, 1);
            WaitForSingleObject(process_, 3000);
        }
        else if (process_)
        {
            DWORD code = 1;
            cleanExit_ = GetExitCodeProcess(process_, &code) && code == 0;
        }
        if (process_) CloseHandle(process_);
        if (job_) CloseHandle(job_);
        if (stop_) CloseHandle(stop_);
        process_ = job_ = stop_ = nullptr;
    }
public:
    explicit ServerProcess(const std::wstring& mode = L"normal")
    {
        static std::atomic_uint next{0};
        const auto token = L"ARTestC02-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(next++);
        root_ = std::filesystem::temp_directory_path() / token;
        if (!std::filesystem::create_directory(root_)) throw std::runtime_error("Cannot create owned server directory.");
        try
        {
            const auto readyName = L"Local\\" + token + L"-ready", stopName = L"Local\\" + token + L"-stop";
            server::Handle ready(CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()));
            stop_ = CreateEventW(nullptr, TRUE, FALSE, stopName.c_str());
            job_ = CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!ready.Get() || !stop_ || !job_ ||
                !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
                throw std::runtime_error("Cannot create server lifecycle controls.");
            const auto executable = Binaries() / "ARTestCLI.UnitTests.exe";
            auto command = L"\"" + executable.wstring() + L"\" --tcp-test-server " + mode +
                L" --ready-file \"" + (root_ / "ready.json").wstring() + L"\" --journal \"" +
                (root_ / "journal.jsonl").wstring() + L"\" --ready-event " + readyName +
                L" --stop-event " + stopName + L" --parent-pid " + std::to_wstring(GetCurrentProcessId());
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION info{};
            if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info))
                throw std::runtime_error("Cannot start test server.");
            process_ = info.hProcess;
            server::Handle thread(info.hThread);
            if (!AssignProcessToJobObject(job_, process_))
            {
                TerminateProcess(process_, 1);
                throw std::runtime_error("Cannot own test server process.");
            }
            if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1)) throw std::runtime_error("Cannot resume server.");
            const HANDLE waits[]{ready.Get(), process_};
            if (WaitForMultipleObjects(2, waits, FALSE, 5000) != WAIT_OBJECT_0)
                throw std::runtime_error("Server failed readiness deadline.");
            std::ifstream input(root_ / "ready.json");
            const auto result = sdk::Json::parse(input);
            if (result.at("pid") != info.dwProcessId) throw std::runtime_error("Readiness PID mismatch.");
            const auto port = result.at("port").get<int>();
            if (port < 1 || port > 65535) throw std::runtime_error("Invalid readiness port.");
            port_ = static_cast<unsigned short>(port);
            std::ofstream(root_ / "mode.json") << sdk::Json{{"mode", std::filesystem::path(mode).string()}}.dump();
        }
        catch (...) { Stop(); std::filesystem::remove_all(root_); throw; }
    }
    ~ServerProcess()
    {
        Stop();
        EXPECT_TRUE(cleanExit_) << "Owned TCP server did not stop cleanly.";
        // Optional acceptance archive: retain the independent server oracle, not live packages.
        try
        {
            wchar_t configured[32768]{};
            if (GetEnvironmentVariableW(L"ARTEST_C02_EVIDENCE_ROOT", configured, 32768))
            {
                const auto archive = std::filesystem::path(configured) / root_.filename();
                std::filesystem::create_directories(archive);
                for (const auto name : {"journal.jsonl", "ready.json", "mode.json"})
                    if (std::filesystem::exists(root_ / name))
                        std::filesystem::copy_file(root_ / name, archive / name);
            }
        }
        catch (...) {} // Destructor cannot obscure an execution failure; the gate checks archives.
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    ServerProcess(const ServerProcess&) = delete;
    ServerProcess& operator=(const ServerProcess&) = delete;
    unsigned short Port() const { return port_; }
    const std::filesystem::path& Root() const { return root_; }
    std::vector<sdk::Json> Journal() const
    {
        std::vector<sdk::Json> result;
        std::ifstream input(root_ / "journal.jsonl");
        std::string line;
        while (std::getline(input, line))
        {
            const auto item = sdk::Json::parse(line, nullptr, false);
            if (!item.is_discarded()) result.push_back(item); // An actively written tail is not readiness.
        }
        return result;
    }
    std::size_t Effects() const
    {
        const auto items = Journal();
        return static_cast<std::size_t>(std::count_if(items.begin(), items.end(),
            [](const sdk::Json& item) { return item.value("event", "") == "applied"; }));
    }
};
} // namespace artest::tests::tcp

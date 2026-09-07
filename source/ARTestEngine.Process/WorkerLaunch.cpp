#include "WorkerLaunch.h"
#include "../ThirdParty/json.hpp"
#include <bcrypt.h>
#include <array>
#include <algorithm>

namespace artest::process
{
std::string RandomBytes(std::size_t size)
{
    std::string bytes(size, '\0');
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()),
                       static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw ProcessError("PROCESS_RANDOM_FAILED", "Cannot initialize secure worker identity.");
    return bytes;
}
std::string Hex(std::string_view bytes)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    for (const auto value : bytes)
    {
        const auto byte = static_cast<unsigned char>(value);
        result += alphabet[byte >> 4];
        result += alphabet[byte & 15];
    }
    return result;
}
namespace
{
std::wstring Quote(const std::wstring &argument)
{
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (const auto value : argument)
    {
        if (value == L'\\') { ++slashes; continue; }
        quoted.append(slashes * (value == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (value == L'"') quoted += L'\\';
        quoted += value;
    }
    quoted.append(slashes * 2, L'\\');
    return quoted + L'"';
}
std::string BootstrapText(const Bootstrap &bootstrap)
{
    std::string pipe;
    for (const auto value : bootstrap.pipe)
    {
        if (value > 127) throw ProcessError("PROCESS_BOOTSTRAP_INVALID", "Pipe names must be generated ASCII.");
        pipe += static_cast<char>(value);
    }
    return nlohmann::json{{"pipe", pipe},
        {"extension", bootstrap.extensionId}, {"fingerprint", bootstrap.fingerprint},
        {"nonce", Hex(bootstrap.nonce)}, {"generation", bootstrap.generation},
        {"parentPid", bootstrap.parentPid}}.dump();
}
std::vector<wchar_t> WorkerEnvironment()
{
    // Do not inherit arbitrary tokens, PYTHONPATH, DOTNET overrides or developer secrets.
    std::vector<std::wstring> entries;
    for (const auto name : {L"SystemRoot", L"TEMP", L"TMP", L"USERPROFILE", L"WINDIR"})
    {
        const auto length = GetEnvironmentVariableW(name, nullptr, 0);
        if (!length) continue;
        std::wstring value(length, L'\0');
        const auto copied = GetEnvironmentVariableW(name, value.data(), length);
        if (!copied || copied >= length) throw ProcessError("PROCESS_ENVIRONMENT_INVALID", "Environment changed during launch.");
        value.resize(copied);
        entries.push_back(std::wstring(name) + L"=" + value);
    }
    wchar_t system[32768]{};
    WinCheck(GetSystemDirectoryW(system, 32768) != 0, "System directory");
    entries.push_back(L"PATH=" + std::wstring(system));
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    std::vector<wchar_t> block;
    for (const auto &entry : entries)
    {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
}
Bootstrap ReadBootstrap(HANDLE value)
{
    UniqueHandle handle(value);
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    WinCheck(ReadFile(handle.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) != FALSE,
             "Read bootstrap");
    auto json = nlohmann::json::parse(buffer.data(), buffer.data() + read);
    Bootstrap bootstrap;
    const auto pipe = json.at("pipe").get<std::string>();
    bootstrap.pipe.assign(pipe.begin(), pipe.end());
    bootstrap.extensionId = json.at("extension");
    bootstrap.fingerprint = json.at("fingerprint");
    bootstrap.generation = json.at("generation");
    bootstrap.parentPid = json.at("parentPid");
    const auto nonce = json.at("nonce").get<std::string>();
    if (nonce.size() != 64 || bootstrap.pipe.find(L"\\\\.\\pipe\\ARTest-") != 0)
        throw ProcessError("PROCESS_BOOTSTRAP_INVALID", "Invalid bootstrap.");
    for (std::size_t i = 0; i < nonce.size(); i += 2)
        bootstrap.nonce += static_cast<char>(std::stoul(nonce.substr(i, 2), nullptr, 16));
    return bootstrap;
}
WorkerProcess LaunchWorker(const std::filesystem::path &executable,
    const std::vector<std::wstring> &arguments, const Bootstrap &bootstrap)
{
    if (!executable.is_absolute() || !std::filesystem::is_regular_file(executable))
        throw ProcessError("PROCESS_EXECUTABLE_INVALID", "Select an explicit existing worker executable.");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    WinCheck(CreatePipe(&read, &write, &attributes, 4096) != FALSE, "Create bootstrap pipe");
    UniqueHandle input(read), output(write);
    WinCheck(SetHandleInformation(output.Get(), HANDLE_FLAG_INHERIT, 0) != FALSE, "Protect bootstrap writer");
    const auto text = BootstrapText(bootstrap);
    if (text.size() > 4000) throw ProcessError("PROCESS_BOOTSTRAP_INVALID", "Bootstrap is too large.");
    DWORD written = 0;
    WinCheck(WriteFile(output.Get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != FALSE &&
             written == text.size(), "Write bootstrap");
    output.Reset();

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    WinCheck(InitializeProcThreadAttributeList(list, 1, 0, &size) != FALSE, "Initialize process attributes");
    struct AttributeGuard { LPPROC_THREAD_ATTRIBUTE_LIST list; ~AttributeGuard() { DeleteProcThreadAttributeList(list); } } guard{list};
    HANDLE inherited[]{input.Get()};
    WinCheck(UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                       sizeof(inherited), nullptr, nullptr) != FALSE, "Restrict inherited handles");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = list;
    std::wstring command = Quote(executable.wstring());
    for (const auto &argument : arguments) command += L" " + Quote(argument);
    command += L" --artest-bootstrap " + std::to_wstring(reinterpret_cast<std::uintptr_t>(input.Get()));
    WorkerProcess child;
    child.job.Reset(CreateJobObjectW(nullptr, nullptr));
    WinCheck(static_cast<bool>(child.job), "Create job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    WinCheck(SetInformationJobObject(child.job.Get(), JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits)) != FALSE, "Configure job");
    PROCESS_INFORMATION process{};
    auto environment = WorkerEnvironment();
    WinCheck(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        environment.data(), executable.parent_path().c_str(), &startup.StartupInfo, &process) != FALSE, "Create worker");
    child.process.Reset(process.hProcess);
    UniqueHandle thread(process.hThread);
    child.pid = process.dwProcessId;
    // Containment precedes all child code. Never run an uncontained worker.
    if (!AssignProcessToJobObject(child.job.Get(), child.process.Get()))
    {
        TerminateProcess(child.process.Get(), 137);
        WaitForSingleObject(child.process.Get(), 3000);
        throw ProcessError("PROCESS_CONTAINMENT_FAILED", "Cannot contain the worker process.");
    }
    if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1))
        throw ProcessError("PROCESS_START_FAILED", "Cannot resume the contained worker.");
    return child;
}
} // namespace artest::process

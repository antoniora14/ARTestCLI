#include "PipeChannel.h"
#include <sddl.h>
#include <algorithm>
#include <array>
#include <limits>

namespace artest::process
{
void WinCheck(bool ok, const char *operation)
{
    if (!ok) throw ProcessError("PROCESS_IO_FAILED",
        std::string(operation) + " failed (Win32 " + std::to_string(GetLastError()) + ").");
}
namespace
{
DWORD Remaining(Clock::time_point deadline)
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<DWORD>((std::min)(left, static_cast<long long>(MAXDWORD - 1)));
}
DWORD Transfer(HANDLE pipe, bool write, char *data, DWORD size, Clock::time_point deadline)
{
    OVERLAPPED operation{};
    UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    WinCheck(static_cast<bool>(event), "CreateEvent");
    operation.hEvent = event.Get();
    DWORD transferred = 0;
    const auto immediate = write ? WriteFile(pipe, data, size, &transferred, &operation)
                                 : ReadFile(pipe, data, size, &transferred, &operation);
    if (!immediate)
    {
        const auto error = GetLastError();
        if (error != ERROR_IO_PENDING)
            throw ProcessError("PROCESS_DISCONNECTED", "Pipe I/O failed (" + std::to_string(error) + ").");
        if (WaitForSingleObject(event.Get(), Remaining(deadline)) != WAIT_OBJECT_0)
        {
            // The OVERLAPPED and its buffer must stay alive until cancellation is drained.
            CancelIoEx(pipe, &operation);
            if (GetOverlappedResult(pipe, &operation, &transferred, TRUE)) return transferred;
            throw ProcessError("PROCESS_IO_TIMEOUT", "Pipe I/O deadline expired.");
        }
        if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE))
            throw ProcessError("PROCESS_DISCONNECTED", "Peer disconnected during pipe I/O.");
    }
    if (!transferred) throw ProcessError("PROCESS_DISCONNECTED", "Peer closed the pipe.");
    return transferred;
}
}
wire::Envelope PipeChannel::Read(Clock::time_point deadline)
{
    std::size_t target = 4;
    for (;;)
    {
        if (m_input.size() >= 4)
        {
            std::uint32_t size = 0;
            for (unsigned i = 0; i != 4; ++i)
                size |= static_cast<std::uint32_t>(static_cast<unsigned char>(m_input[i])) << (8 * i);
            if (!size || size > MaxFrameBytes)
                throw ProcessError("PROCESS_FRAME_INVALID", "Invalid length-prefixed frame.");
            target = 4 + size;
            if (m_input.size() == target)
            {
                auto message = Decode(std::string_view(m_input.data() + 4, size));
                m_input.clear();
                return message;
            }
        }
        if (Clock::now() >= deadline) throw ProcessError("PROCESS_IO_TIMEOUT", "Read polling deadline expired.");
        std::array<char, 8192> buffer{};
        const auto size = static_cast<DWORD>((std::min)(target - m_input.size(), buffer.size()));
        const auto received = Transfer(Handle(), false, buffer.data(), size, deadline);
        m_input.insert(m_input.end(), buffer.data(), buffer.data() + received);
    }
}
void PipeChannel::WriteBytes(std::string_view bytes, Clock::time_point deadline)
{
    while (!bytes.empty())
    {
        if (Clock::now() >= deadline) throw ProcessError("PROCESS_IO_TIMEOUT", "Write deadline expired.");
        auto count = Transfer(Handle(), true, const_cast<char *>(bytes.data()),
                              static_cast<DWORD>(bytes.size()), deadline);
        bytes.remove_prefix(count);
    }
}
void PipeChannel::Write(const wire::Envelope &message, Clock::time_point deadline)
{
    const auto bytes = Encode(message);
    std::string frame(4, '\0');
    for (unsigned i = 0; i != 4; ++i) frame[i] = static_cast<char>((bytes.size() >> (8 * i)) & 0xff);
    frame += bytes;
    WriteBytes(frame, deadline);
}
UniqueHandle CreateLocalPipe(const std::wstring &name)
{
    UniqueHandle token;
    HANDLE value = nullptr;
    WinCheck(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &value) != FALSE, "OpenProcessToken");
    token.Reset(value);
    DWORD size = 0;
    GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> storage(size);
    WinCheck(GetTokenInformation(token.Get(), TokenUser, storage.data(), size, &size) != FALSE, "TokenUser");
    LPWSTR sid = nullptr;
    WinCheck(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid, &sid) != FALSE,
             "ConvertSidToStringSid");
    std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    WinCheck(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                                 &descriptor, nullptr) != FALSE, "Pipe ACL");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    UniqueHandle pipe(CreateNamedPipeW(name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 65536, 65536, 0, &attributes));
    LocalFree(descriptor);
    WinCheck(static_cast<bool>(pipe), "CreateNamedPipe");
    return pipe;
}
void ConnectLocalPipe(HANDLE pipe, HANDLE child, Clock::time_point deadline)
{
    UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    WinCheck(static_cast<bool>(event), "CreateEvent");
    OVERLAPPED operation{};
    operation.hEvent = event.Get();
    if (ConnectNamedPipe(pipe, &operation)) return;
    const auto error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) return;
    WinCheck(error == ERROR_IO_PENDING, "ConnectNamedPipe");
    HANDLE waits[]{event.Get(), child};
    if (WaitForMultipleObjects(2, waits, FALSE, Remaining(deadline)) != WAIT_OBJECT_0)
    {
        CancelIoEx(pipe, &operation);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, &operation, &ignored, TRUE);
        throw ProcessError("PROCESS_START_FAILED", "Worker exited or exceeded the connection deadline.");
    }
    DWORD ignored = 0;
    WinCheck(GetOverlappedResult(pipe, &operation, &ignored, FALSE) != FALSE, "Connect completion");
}
UniqueHandle OpenLocalPipe(const std::wstring &name)
{
    UniqueHandle pipe(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    WinCheck(static_cast<bool>(pipe), "Open named pipe");
    return pipe;
}
} // namespace artest::process

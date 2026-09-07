#pragma once
#include "PipeChannel.h"
#include <filesystem>

namespace artest::process
{
struct Bootstrap
{
    std::wstring pipe;
    std::string extensionId, fingerprint, nonce;
    std::uint64_t generation = 0;
    DWORD parentPid = 0;
};
struct WorkerProcess
{
    UniqueHandle job, process;
    DWORD pid = 0;
};
// The inherited handle transports secrets; argv contains only its numeric handle value.
Bootstrap ReadBootstrap(HANDLE handle);
WorkerProcess LaunchWorker(const std::filesystem::path &executable,
    const std::vector<std::wstring> &arguments, const Bootstrap &bootstrap);
std::string RandomBytes(std::size_t size);
std::string Hex(std::string_view bytes);
} // namespace artest::process

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Protocol.h"
#include <chrono>
#include <utility>
#include <vector>

namespace artest::process
{
using Clock = std::chrono::steady_clock;
class UniqueHandle
{
  public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) noexcept : m_value(value) {}
    ~UniqueHandle() { Reset(); }
    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;
    UniqueHandle(UniqueHandle &&other) noexcept : m_value(other.Release()) {}
    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    { if (this != &other) Reset(other.Release()); return *this; }
    HANDLE Get() const noexcept { return m_value; }
    explicit operator bool() const noexcept { return m_value && m_value != INVALID_HANDLE_VALUE; }
    HANDLE Release() noexcept { return std::exchange(m_value, nullptr); }
    void Reset(HANDLE value = nullptr) noexcept { if (*this) CloseHandle(m_value); m_value = value; }
  private:
    HANDLE m_value = nullptr;
};
void WinCheck(bool ok, const char *operation);

// Single owner, reentrant between complete messages; not concurrently readable/writable.
// Partial reads survive a polling timeout instead of discarding framing state.
class PipeChannel
{
  public:
    explicit PipeChannel(UniqueHandle pipe) noexcept : m_pipe(std::move(pipe)) {}
    HANDLE Handle() const noexcept { return m_pipe.Get(); }
    wire::Envelope Read(Clock::time_point deadline);
    void Write(const wire::Envelope &message, Clock::time_point deadline);
    void WriteBytes(std::string_view bytes, Clock::time_point deadline);
  private:
    UniqueHandle m_pipe;
    std::vector<char> m_input;
};
UniqueHandle CreateLocalPipe(const std::wstring &name);
void ConnectLocalPipe(HANDLE pipe, HANDLE child, Clock::time_point deadline);
UniqueHandle OpenLocalPipe(const std::wstring &name);
} // namespace artest::process

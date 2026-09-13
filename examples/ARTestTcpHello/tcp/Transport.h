#pragma once
// Private Windows transport: no networking types or ownership cross the SDK ABI.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ARTest/Context.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace artest::tcphello
{
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct Failure : std::runtime_error
{
    sdk::Status cause;
    Failure(sdk::Status status, std::string message)
        : std::runtime_error(std::move(message)), cause(status) {}
};

class Winsock final
{
public:
    Winsock()
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw Failure(sdk::Status::ResourceUnavailable, "Winsock initialization failed.");
    }
    ~Winsock() { WSACleanup(); }
    Winsock(const Winsock&) = delete;
    Winsock& operator=(const Winsock&) = delete;
};

class Socket final
{
    SOCKET value_ = INVALID_SOCKET;
public:
    Socket() = default;
    explicit Socket(SOCKET value) : value_(value) {}
    ~Socket() { Reset(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : value_(std::exchange(other.value_, INVALID_SOCKET)) {}
    Socket& operator=(Socket&& other) noexcept
    {
        if (this != &other) { Reset(); value_ = std::exchange(other.value_, INVALID_SOCKET); }
        return *this;
    }
    void Reset() noexcept { if (value_ != INVALID_SOCKET) closesocket(std::exchange(value_, INVALID_SOCKET)); }
    SOCKET Get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != INVALID_SOCKET; }
};

inline void Nonblocking(SOCKET socket)
{
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled) != 0)
        throw Failure(sdk::Status::ResourceUnavailable, "Cannot configure nonblocking socket.");
}

class Budget final
{
    Clock::time_point expires_;
    std::function<sdk::Result()> checkpoint_;
public:
    explicit Budget(int milliseconds, std::function<sdk::Result()> checkpoint)
        : expires_(Clock::now() + std::chrono::milliseconds(milliseconds)), checkpoint_(std::move(checkpoint)) {}
    void Check() const
    {
        if (auto result = checkpoint_(); !result) throw Failure(result.Code(), result.Message());
        if (Clock::now() >= expires_) throw Failure(sdk::Status::TimedOut, "TCP operation budget expired.");
    }
    std::chrono::microseconds Slice() const
    {
        Check();
        return std::max(1us, std::min(10000us,
            std::chrono::duration_cast<std::chrono::microseconds>(expires_ - Clock::now())));
    }
    void Wait(SOCKET socket, bool writing) const
    {
        const auto slice = Slice();
        fd_set ready, error;
        FD_ZERO(&ready); FD_ZERO(&error); FD_SET(socket, &ready); FD_SET(socket, &error);
        timeval timeout{0, static_cast<long>(slice.count())};
        const int result = select(0, writing ? nullptr : &ready, writing ? &ready : nullptr, &error, &timeout);
        if (result == SOCKET_ERROR || FD_ISSET(socket, &error))
            throw Failure(sdk::Status::ResourceUnavailable, "TCP socket wait failed.");
        Check();
    }
    std::unique_lock<std::timed_mutex> Lock(std::timed_mutex& mutex) const
    {
        std::unique_lock lock(mutex, std::defer_lock);
        while (!lock.try_lock_for(Slice())) Check();
        Check();
        return lock;
    }
};

inline Socket Connect(unsigned short port, const Budget& budget)
{
    budget.Check();
    Socket socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!socket) throw Failure(sdk::Status::ResourceUnavailable, "Cannot create TCP socket.");
    Nonblocking(socket.Get());
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // No DNS, interfaces or remote hosts.
    address.sin_port = htons(port);
    if (connect(socket.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            throw Failure(sdk::Status::ResourceUnavailable, "TCP connection refused or unavailable.");
        for (;;)
        {
            budget.Wait(socket.Get(), true);
            int error = 0;
            int size = sizeof(error);
            if (getsockopt(socket.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0 || error != 0)
                throw Failure(sdk::Status::ResourceUnavailable, "TCP connection failed.");
            sockaddr_in peer{};
            int peerSize = sizeof(peer);
            if (getpeername(socket.Get(), reinterpret_cast<sockaddr*>(&peer), &peerSize) == 0) break;
        }
    }
    budget.Check();
    return socket;
}

inline void Send(SOCKET socket, const std::string& frame, const Budget& budget, bool& possiblySent)
{
    std::size_t offset = 0;
    while (offset < frame.size())
    {
        budget.Check();
        const auto size = static_cast<int>(std::min<std::size_t>(frame.size() - offset, 65536));
        const int count = send(socket, frame.data() + offset, size, 0);
        if (count > 0) { possiblySent = true; offset += static_cast<std::size_t>(count); }
        else if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) budget.Wait(socket, true);
        else throw Failure(sdk::Status::ResourceUnavailable, "TCP write failed.");
    }
}

inline constexpr std::size_t MaximumFrame = 1024;
inline std::string Receive(SOCKET socket, const Budget& budget)
{
    std::string frame;
    frame.reserve(MaximumFrame);
    for (;;)
    {
        budget.Check();
        char buffer[256];
        const int count = recv(socket, buffer, sizeof(buffer), 0);
        if (count > 0)
        {
            const auto size = static_cast<std::size_t>(count);
            if (frame.size() + size > MaximumFrame)
                throw Failure(sdk::Status::ExtensionFailure, "TCP response exceeds 1024 bytes.");
            frame.append(buffer, size);
            const auto delimiter = frame.find('\n');
            if (delimiter != std::string::npos)
            {
                if (delimiter != frame.size() - 1)
                    throw Failure(sdk::Status::ExtensionFailure, "Unexpected trailing TCP response bytes.");
                budget.Check();
                return frame;
            }
        }
        else if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) budget.Wait(socket, false);
        else throw Failure(sdk::Status::ResourceUnavailable, "TCP peer disconnected before acknowledgement.");
    }
}
} // namespace artest::tcphello

#pragma once
#include "../tcp/Transport.h"
#include <Windows.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace artest::tcphello::server
{
// Injection is supplied only by the executable under tests; normal Main has no fault modes.
struct Reply
{
    std::string frame;
    std::chrono::milliseconds delay{0};
    std::size_t fragment = MaximumFrame;
    std::chrono::milliseconds interval{0};
    bool disconnect = false;
};
using Policy = std::function<Reply(const sdk::Json&, const sdk::Json&)>;
class Handle final
{
    HANDLE value_;
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const { return value_; }
};
struct Options
{
    std::filesystem::path readyFile, journal;
    std::wstring readyEvent, stopEvent;
    DWORD parent = 0;
    unsigned short port = 0;
};
inline Options Parse(int argc, wchar_t** argv)
{
    Options result;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::wstring key = argv[i], value = argv[i + 1];
        if (key == L"--ready-file") result.readyFile = value;
        else if (key == L"--journal") result.journal = value;
        else if (key == L"--ready-event") result.readyEvent = value;
        else if (key == L"--stop-event") result.stopEvent = value;
        else if (key == L"--parent-pid") result.parent = std::stoul(value);
        else if (key == L"--port")
        {
            const auto port = std::stoul(value);
            if (port > 65535) throw std::invalid_argument("Invalid loopback port.");
            result.port = static_cast<unsigned short>(port);
        }
        else throw std::invalid_argument("Unknown simulator argument.");
    }
    if (argc % 2 != 1 || result.readyFile.empty() || result.journal.empty() ||
        result.readyEvent.empty() || result.stopEvent.empty() || result.parent == 0)
        throw std::invalid_argument("Require ready-file, journal, ready-event, stop-event and parent-pid.");
    return result;
}
inline int Run(const Options& options, Policy policy = {})
{
    Winsock winsock;
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, options.readyEvent.c_str()));
    Handle stop(OpenEventW(SYNCHRONIZE, FALSE, options.stopEvent.c_str()));
    Handle parent(OpenProcess(SYNCHRONIZE, FALSE, options.parent));
    if (!ready.Get() || !stop.Get() || !parent.Get()) throw std::runtime_error("Cannot open lifecycle handles.");
    std::ofstream journal(options.journal, std::ios::binary | std::ios::trunc);
    if (!journal) throw std::runtime_error("Cannot open server journal.");
    auto record = [&journal](sdk::Json entry)
    {
        journal << entry.dump() << '\n';
        journal.flush(); // Independent effect oracle precedes acknowledgement, not durable physical proof.
        if (!journal) throw std::runtime_error("Server journal write failed.");
    };
    Socket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listener) throw std::runtime_error("Cannot create listener.");
    BOOL exclusive = TRUE;
    if (setsockopt(listener.Get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0)
        throw std::runtime_error("Cannot configure exclusive listener.");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(options.port);
    if (bind(listener.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener.Get(), 16) != 0) throw std::runtime_error("Cannot bind/listen on loopback.");
    Nonblocking(listener.Get());
    int addressSize = sizeof(address);
    if (getsockname(listener.Get(), reinterpret_cast<sockaddr*>(&address), &addressSize) != 0)
        throw std::runtime_error("Cannot read assigned port.");
    record({{"event", "listening"}, {"port", ntohs(address.sin_port)}});
    {
        std::ofstream file(options.readyFile, std::ios::binary | std::ios::trunc);
        file << sdk::Json{{"pid", GetCurrentProcessId()}, {"port", ntohs(address.sin_port)}}.dump();
        file.flush();
        if (!file) throw std::runtime_error("Cannot publish readiness.");
    }
    if (!SetEvent(ready.Get())) throw std::runtime_error("Cannot signal readiness.");
    struct Client
    {
        Socket socket;
        std::uint64_t connection;
        std::uint64_t lastId = 0;
        double voltage = 0;
        bool hello = false, closeAfterReply = false;
        std::string input;
        Reply reply;
        std::size_t sent = 0;
        Clock::time_point due = Clock::now(), active = Clock::now();
    };
    std::vector<Client> clients;
    std::uint64_t connection = 0;
    const HANDLE lifecycle[]{stop.Get(), parent.Get()};
    for (;;)
    {
        const auto waiting = WaitForMultipleObjects(2, lifecycle, FALSE, 5);
        if (waiting == WAIT_FAILED) throw std::runtime_error("Lifecycle wait failed.");
        if (waiting != WAIT_TIMEOUT) break;
        if (Socket accepted{accept(listener.Get(), nullptr, nullptr)}; accepted)
        {
            Nonblocking(accepted.Get());
            if (clients.size() < 16)
            {
                clients.push_back(Client{.socket = std::move(accepted), .connection = ++connection});
                record({{"event", "connected"}, {"connection", connection}});
            }
        }
        for (auto& client : clients)
        {
            if (!client.socket) continue;
            try
            {
                if (Clock::now() - client.active > 15s) throw std::runtime_error("Idle client expired.");
                if (client.sent < client.reply.frame.size())
                {
                    if (Clock::now() < client.due) continue;
                    const auto size = std::min(client.reply.fragment, client.reply.frame.size() - client.sent);
                    const int count = send(client.socket.Get(), client.reply.frame.data() + client.sent, static_cast<int>(size), 0);
                    if (count > 0)
                    {
                        client.sent += static_cast<std::size_t>(count);
                        client.due = Clock::now() + client.reply.interval;
                    }
                    else if (count == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
                        throw std::runtime_error("Response write failed.");
                    if (client.sent == client.reply.frame.size() && client.closeAfterReply) client.socket.Reset();
                    continue;
                }
                char buffer[256];
                const int count = recv(client.socket.Get(), buffer, sizeof(buffer), 0);
                if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
                if (count <= 0) throw std::runtime_error("Client disconnected.");
                if (client.input.size() + static_cast<std::size_t>(count) > MaximumFrame)
                    throw std::runtime_error("Request exceeds bound.");
                client.input.append(buffer, static_cast<std::size_t>(count));
                const auto newline = client.input.find('\n');
                if (newline == std::string::npos) continue;
                if (newline != client.input.size() - 1) throw std::runtime_error("Pipelined requests forbidden.");
                auto request = sdk::Json::parse(client.input);
                client.input.clear();
                if (request.at("v") != 1 || !request.at("id").is_number_unsigned() ||
                    request.at("id").get<std::uint64_t>() <= client.lastId)
                    throw std::runtime_error("Invalid protocol version or sequence.");
                client.lastId = request.at("id").get<std::uint64_t>();
                const auto op = request.at("op").get<std::string>();
                if (op == "hello" && !client.hello) client.hello = true;
                else if (!client.hello) throw std::runtime_error("hello required.");
                else if (op == "apply")
                {
                    if (!request.at("value").is_number()) throw std::runtime_error("Invalid voltage.");
                    const double value = request.at("value").get<double>();
                    if (!std::isfinite(value) || value < 0 || value > 60) throw std::runtime_error("Invalid voltage.");
                    client.voltage = value;
                    record({{"event", "applied"}, {"connection", client.connection}, {"id", client.lastId}, {"value", value}});
                }
                else if (op != "read" && op != "close") throw std::runtime_error("Unknown operation.");
                client.active = Clock::now();
                sdk::Json response{{"v", 1}, {"id", client.lastId}, {"op", op}, {"ok", true}};
                if (op == "apply" || op == "read") { response["value"] = client.voltage; response["unit"] = "V"; }
                record({{"event", "request"}, {"connection", client.connection}, {"op", op}, {"id", client.lastId}});
                client.reply = policy ? policy(request, response) : Reply{.frame = response.dump() + "\n"};
                client.sent = 0;
                client.due = Clock::now() + client.reply.delay;
                client.closeAfterReply = op == "close";
                if (client.reply.disconnect) client.socket.Reset();
            }
            catch (const std::exception& error)
            {
                record({{"event", "disconnected"}, {"connection", client.connection}, {"reason", error.what()}});
                client.socket.Reset();
            }
        }
        std::erase_if(clients, [](const Client& client) { return !client.socket; });
    }
    record({{"event", "stopped"}, {"openConnections", clients.size()}});
    return 0;
}
} // namespace artest::tcphello::server

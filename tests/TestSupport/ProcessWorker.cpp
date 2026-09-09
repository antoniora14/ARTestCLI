#include "ARTestEngine.Process/WorkerSupervisor.h"
#include "ThirdParty/json.hpp"
#include <map>

using namespace artest::process;
namespace
{
void Raw(PipeChannel &pipe, const wire::Envelope &message, bool fragmented = false)
{
    const auto bytes = message.SerializeAsString();
    std::string frame(4, '\0');
    for (unsigned i = 0; i < 4; ++i) frame[i] = static_cast<char>((bytes.size() >> (8 * i)) & 255);
    frame += bytes;
    if (fragmented)
    {
        pipe.WriteBytes(std::string_view(frame).substr(0, 2), Clock::now() + std::chrono::seconds{1});
        Sleep(40); // Deliberately split a prefix across multiple supervisor polling deadlines.
        frame.erase(0, 2);
    }
    pipe.WriteBytes(frame, Clock::now() + std::chrono::seconds{1});
}
}
int RunProcessWorker(int argc, char **argv)
{
    try
    {
        const std::string mode = argc > 2 ? argv[2] : "normal";
        if (mode == "orphan") { Sleep(INFINITE); return 0; }
        HANDLE inherited = nullptr;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string_view(argv[i]) == "--artest-bootstrap")
                inherited = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(argv[i + 1])));
        const auto bootstrap = ReadBootstrap(inherited);
        if (mode == "no-connect") { Sleep(INFINITE); return 0; }
        PipeChannel pipe(OpenLocalPipe(bootstrap.pipe));
        ULONG server = 0;
        WinCheck(GetNamedPipeServerProcessId(pipe.Handle(), &server) != FALSE &&
                 server == bootstrap.parentPid, "Server identity");
        auto hello = Message(bootstrap.generation, 1);
        auto &identity = *hello.mutable_hello();
        identity.set_extension_id(mode == "bad-identity" ? "com.artest.wrong" : bootstrap.extensionId);
        identity.set_fingerprint(bootstrap.fingerprint);
        identity.set_nonce(bootstrap.nonce);
        if (mode == "bad-version") hello.set_minor(999);
        if (mode == "bad-nonce") identity.set_nonce(std::string(32, 'x'));
        Raw(pipe, hello);
        const auto acknowledgement = pipe.Read(Clock::now() + std::chrono::seconds{3});
        if (!acknowledgement.has_hello() || !acknowledgement.hello().acknowledged()) return 2;
        std::map<std::uint64_t, wire::Envelope> waiting, services;
        std::map<std::uint64_t, bool> received;
        std::uint64_t nextService = 3;
        for (;;)
        {
            wire::Envelope message;
            try { message = pipe.Read(Clock::now() + std::chrono::milliseconds{20}); }
            catch (const ProcessError &error)
            { if (error.code == "PROCESS_IO_TIMEOUT") continue; throw; }
            auto response = Message(bootstrap.generation, message.correlation());
            if (message.has_cancel())
            {
                if (!received.contains(message.correlation()) || received.at(message.correlation()))
                    return 4;
                received.at(message.correlation()) = true;
                if (waiting.contains(message.correlation()))
                {
                    const bool terminalFirst =
                        waiting.at(message.correlation()).request().operation_id() == "uncertain-wait";
                    response.mutable_cancel()->set_acknowledged(true);
                    if (!terminalFirst) pipe.Write(response, Clock::now() + std::chrono::seconds{1});
                    if (mode != "ignore-cancel")
                    {
                        response.clear_cancel();
                        *response.mutable_response() = Response(wire::CANCELLED, "Worker cancellation observed.");
                        if (waiting.at(message.correlation()).request().operation_id() == "uncertain-wait")
                        {
                            auto *result = response.mutable_response();
                            result->set_status(wire::EXTENSION_FAILURE);
                            result->set_effect_indeterminate(true);
                            result->set_diagnostic("C01 lost acknowledgement");
                            result->mutable_payload()->set_schema_id("artest.schema.process-test.v1");
                            result->mutable_payload()->set_json(R"({"effect":"unconfirmed"})");
                        }
                        pipe.Write(response, Clock::now() + std::chrono::seconds{1});
                        if (terminalFirst)
                        {
                            // Force a result-before-ACK race, including a scheduling gap.
                            Sleep(20);
                            response.clear_response();
                            response.mutable_cancel()->set_acknowledged(true);
                            pipe.Write(response, Clock::now() + std::chrono::seconds{1});
                        }
                        waiting.erase(message.correlation());
                    }
                }
                else
                {
                    // A service/terminal response may cross cancellation in flight.
                    response.mutable_cancel()->set_acknowledged(true);
                    pipe.Write(response, Clock::now() + std::chrono::seconds{1});
                }
                continue;
            }
            if (message.has_response())
            {
                const auto original = services.at(message.correlation());
                services.erase(message.correlation());
                response.set_correlation(original.correlation());
                *response.mutable_response() = message.response();
                pipe.Write(response, Clock::now() + std::chrono::seconds{1});
                continue;
            }
            if (!message.has_request()) return 3;
            received.emplace(message.correlation(), false);
            if (received.size() > 64) received.erase(received.begin());
            const auto &request = message.request();
            if (request.operation_id() == "crash") ExitProcess(17);
            if (request.operation_id() == "stall") { Sleep(INFINITE); return 0; }
            if (request.operation_id() == "wait" || request.operation_id() == "uncertain-wait")
            { waiting.emplace(message.correlation(), message); continue; }
            if (request.operation_id() == "service" || request.operation_id() == "cancel-service")
            {
                if (request.operation_id() == "cancel-service")
                {
                    auto event = Message(bootstrap.generation, message.correlation());
                    event.mutable_event()->set_message("Cancel before requesting another device operation.");
                    pipe.Write(event, Clock::now() + std::chrono::seconds{1});
                }
                auto call = Message(bootstrap.generation, nextService);
                nextService += 2;
                call.set_parent(message.correlation());
                call.mutable_request()->set_operation(wire::INVOKE_SERVICE);
                call.mutable_request()->set_component(8);
                call.mutable_request()->set_operation_id("read");
                services.emplace(call.correlation(), message);
                pipe.Write(call, Clock::now() + std::chrono::seconds{1});
                continue;
            }
            if (request.operation_id() == "oversized")
            {
                pipe.WriteBytes(std::string("\xff\xff\xff\x7f", 4), Clock::now() + std::chrono::seconds{1});
                continue;
            }
            if (request.operation_id() == "logs")
            {
                for (int i = 0; i < 300; ++i)
                {
                    auto event = Message(bootstrap.generation, message.correlation());
                    event.mutable_event()->set_message("bounded log");
                    pipe.Write(event, Clock::now() + std::chrono::seconds{1});
                }
            }
            auto &result = *response.mutable_response();
            result.set_status(request.operation_id() == "error" ? wire::EXTENSION_FAILURE : wire::OK);
            result.set_diagnostic(request.operation_id() == "error" ? "SIMULATED_EXTENSION_ERROR" : "");
            result.mutable_payload()->set_schema_id("artest.schema.process-test.v1");
            result.mutable_payload()->set_json(nlohmann::json{
                {"operation", request.operation_id()}, {"remainingMs", request.remaining_ms()},
                {"secretInherited", GetEnvironmentVariableW(L"ARTEST_TEST_SECRET", nullptr, 0) != 0}}.dump());
            if (request.operation_id() == "spawn-child")
            {
                wchar_t executable[32768]{};
                GetModuleFileNameW(nullptr, executable, 32768);
                auto command = L"\"" + std::wstring(executable) + L"\" --artest-process-worker orphan";
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION child{};
                WinCheck(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                         nullptr, nullptr, &startup, &child) != FALSE, "Spawn job descendant");
                UniqueHandle childProcess(child.hProcess), childThread(child.hThread);
                result.set_handle(child.dwProcessId);
            }
            if (request.operation_id() == "stale") response.set_generation(bootstrap.generation + 1);
            Raw(pipe, response, request.operation_id() == "fragmented");
            if (request.operation_id() == "duplicate") Raw(pipe, response);
            if (request.operation() == wire::STOP) return 0;
        }
    }
    catch (...) { return 5; }
}

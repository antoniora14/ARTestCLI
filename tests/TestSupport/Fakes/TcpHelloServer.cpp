#include "../../../examples/ARTestTcpHello/simulator/Server.h"
#include <shellapi.h>
namespace sdk = artest::sdk;

int RunTcpHelloServer()
{
    using namespace artest::tcphello;
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    try
    {
        if (argc < 3) throw std::runtime_error("Missing test server mode.");
        const std::wstring mode = argv[2];
        const auto options = server::Parse(argc - 2, argv + 2);
        LocalFree(argv);
        argv = nullptr;
        return server::Run(options, [mode](const sdk::Json& request, const sdk::Json& response)
        {
            const auto op = request.at("op").get<std::string>();
            server::Reply reply{.frame = response.dump() + "\n"};
            if (mode == L"fragmented") { reply.fragment = 3; reply.interval = 1ms; }
            if (op == "hello" && mode == L"partial-init") reply.disconnect = true;
            if (op == "close" && mode == L"close-timeout") reply.delay = 10s;
            if (op == "read" && mode == L"late-read") reply.delay = 500ms;
            if (op != "apply") return reply;
            if (mode == L"lost-ack") reply.frame.clear();
            if (mode == L"disconnect") reply.disconnect = true;
            if (mode == L"late") reply.delay = 500ms;
            if (mode == L"slow-drip") { reply.fragment = 1; reply.interval = 30ms; }
            if (mode == L"malformed") reply.frame = "{invalid}\n";
            if (mode == L"oversized") reply.frame = std::string(1025, 'x') + "\n";
            if (mode == L"wrong-id") { auto altered = response; altered["id"] = 999; reply.frame = altered.dump() + "\n"; }
            if (mode == L"wrong-op") { auto altered = response; altered["op"] = "read"; reply.frame = altered.dump() + "\n"; }
            if (mode == L"wrong-version") { auto altered = response; altered["v"] = 2; reply.frame = altered.dump() + "\n"; }
            return reply;
        });
    }
    catch (const std::exception& error)
    {
        if (argv) LocalFree(argv);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "Server.h"
int wmain(int argc, wchar_t** argv)
{
    try { return artest::tcphello::server::Run(artest::tcphello::server::Parse(argc, argv)); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

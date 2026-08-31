#include "CliApplication.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index)
    {
        arguments.emplace_back(argv[index]);
    }

    artest::cli::CliApplication application(std::cin, std::cout, std::cerr);
    return application.Run(arguments);
}

#include <gtest/gtest.h>
#include <string_view>
int RunProcessWorker(int argc, char **argv);

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--artest-process-worker")
        return RunProcessWorker(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

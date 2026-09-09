#pragma once
#include "EffectsExtension.h"
#include <filesystem>

namespace artest::tests::effects
{
inline void AddPackage(const std::filesystem::path &root, const std::filesystem::path &binaries)
{
    const auto package = root / "C01FaultExtension";
    std::filesystem::create_directories(package / "schemas");
    std::filesystem::copy_file(binaries / "C01FaultExtension.dll", package / "C01FaultExtension.dll");
    const auto metadata = GenerateMetadata(Define(), "C01FaultExtension.dll");
    std::ofstream(package / "artest-extension.json") << metadata.manifest.dump(2);
    for (const auto &[path, schema] : metadata.schemas)
        std::ofstream(package / path) << schema.dump(2);
}
inline Json Plan(const std::filesystem::path &effect, const std::string &mode = "lost",
                 const std::string &action = "propagate")
{
    Json step{{"stepId", 1}, {"name", "com.artest.test.command.effects"}, {"instrument", "PS1"},
        {"params", {{"action", action}}},
        {"policy", {{"maxAttempts", 3}, {"timeoutMs", 2000}, {"onFailure", "continue"}}}};
    auto next = step;
    next["stepId"] = 2;
    return {{"format", "ARTest.Script"}, {"version", 1},
        {"instruments", Json::array({{{"type", "com.artest.test.driver.effects"}, {"id", "PS1"},
            {"config", {{"effectFile", effect.string()}, {"mode", mode}}}}})},
        {"commands", Json::array({step, next})}};
}
inline std::string Read(const std::filesystem::path &path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
}

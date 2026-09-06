#include <ARTestEngineClient.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>

// Reuse the Engine validators without compiling a plan, starting a session,
// or constructing an instrument/command. Only trusted DLLs may be inspected.
int wmain(int argc, wchar_t **argv)
{
    using Json = nlohmann::json;
    try
    {
        if (argc != 2) throw std::invalid_argument("Usage: ARTestSdkValidate <isolated-catalog>");
        const auto utf8 = std::filesystem::absolute(argv[1]).u8string();
        const std::string root(utf8.begin(), utf8.end());
        artest::sdk::EngineClient engine;
        const auto created = engine.Create(R"({"loadDefaultCatalog":false})");
        if (!created.Succeeded()) throw std::runtime_error(created.message);
        std::string text;
        const auto checked = engine.ValidateCatalog(root, text);
        if (!checked.Succeeded()) throw std::runtime_error(checked.message);
        auto report = Json::parse(text);
        if (!report.value("valid", false) || report.at("packages").size() != 1)
        {
            std::cout << Json{{"valid", false}, {"catalog", report}}.dump() << '\n';
            return 1;
        }
        const auto activated = engine.RefreshCatalog(root);
        const auto snapshot = engine.GetCatalogSnapshot(text);
        if (!snapshot.Succeeded()) throw std::runtime_error(snapshot.message);
        report = Json::parse(text);
        const bool valid = activated.Succeeded() && report.value("valid", false) &&
                           report.value("status", "") == "active";
        std::cout << Json{{"valid", valid}, {"catalog", report},
                          {"message", activated.message}}.dump() << '\n';
        return valid ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "ARTESTPKG001: " << error.what() << '\n';
        return 1;
    }
}

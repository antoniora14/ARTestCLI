#include <ARTestEngineApi.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Test-only host: activate descriptors through the public ABI, never create a
// session/plan/component. The production CLI doctor has no Python mapping option.
int wmain(int argc, wchar_t **argv)
{
    if (argc != 4) return 2;
    const auto module = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) return 3;
    const auto address = GetProcAddress(module, "ARTestEngine_QueryApi");
    ARTestEngineQueryApiFn query{};
    static_assert(sizeof(query) == sizeof(address));
    std::memcpy(&query, &address, sizeof(query));
    ARTestEngineApiV0 api{}; api.struct_size = sizeof(api);
    char diagnostic[4096]{};
    ARTestErrorBuffer error{sizeof(error), 0, diagnostic, sizeof(diagnostic), 0};
    ARTestEngineHandle engine{};
    int result = 1;
    try {
        if (!query || query(ARTEST_ENGINE_API_MAJOR, ARTEST_ENGINE_API_MINOR, &api, &error) != ARTEST_STATUS_OK)
            throw std::runtime_error(diagnostic);
        std::ifstream mapping{std::filesystem::path{argv[3]}};
        const auto options = nlohmann::json{{"loadDefaultCatalog", false}, {"pythonEnvironments", nlohmann::json::parse(mapping)}}.dump();
        const std::string schema = "artest.schema.generic-json.v1", media = "application/json; charset=utf-8";
        const ARTestPayloadView payload{sizeof(payload), ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            {schema.data(), schema.size()}, {media.data(), media.size()},
            {reinterpret_cast<const uint8_t *>(options.data()), options.size()}};
        if (api.create_engine(&payload, &engine, &error) != ARTEST_STATUS_OK) throw std::runtime_error(diagnostic);
        const auto utf8 = std::filesystem::path{argv[2]}.u8string();
        const ARTestStringView root{reinterpret_cast<const char *>(utf8.data()), utf8.size()};
        if (api.refresh_catalog(engine, root, &error) != ARTEST_STATUS_OK) throw std::runtime_error(diagnostic);
        std::cout << "Metadata activation PASSED; no session or Test plan created.\n";
        result = 0;
    } catch (const std::exception &exception) { std::cerr << exception.what() << '\n'; }
    if (engine) api.destroy_engine(engine);
    FreeLibrary(module);
    return result;
}

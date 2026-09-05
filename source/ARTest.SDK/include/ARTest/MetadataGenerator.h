#pragma once

#include "Metadata.h"
#include <iostream>

namespace artest::sdk
{
// Compile the same definition source as a build executable. The function may
// register factory pointers but must never invoke them or perform hardware I/O.
template <Extension (*Define)()>
int RunMetadataGenerator(int argc, char **argv) noexcept
{
    try
    {
        if (argc != 2)
            throw std::invalid_argument("Usage: metadata-generator <extension.dll>");
        const auto bundle = GenerateMetadata(Define(), argv[1]);
        Json files = Json::object();
        for (const auto &[path, schema] : bundle.schemas) files[path] = schema.dump(2) + "\n";
        const Json serialized = {{"format", "ARTest.MetadataBundle"}, {"version", 1},
                                 {"manifest", bundle.manifest},
                                 {"manifestText", bundle.manifest.dump(2) + "\n"}, {"schemas", files}};
        // ASCII envelope avoids Windows console code-page loss; the inner file
        // strings are decoded back to UTF-8 by the build publisher.
        std::cout << serialized.dump(2, ' ', true) << '\n';
        if (!std::cout) throw std::runtime_error("Unable to write metadata output.");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "ARTESTMETA001: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "ARTESTMETA001: Unknown metadata generation failure.\n";
        return 1;
    }
}
} // namespace artest::sdk

#define ARTEST_GENERATE_METADATA(DefineFunction) \
    int main(int argc, char **argv) \
    { return ::artest::sdk::RunMetadataGenerator<DefineFunction>(argc, argv); }

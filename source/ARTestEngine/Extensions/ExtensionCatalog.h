#pragma once

#include "../../ARTestEngine.Core/Diagnostics.h"
#include "../../ThirdParty/json.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace artest::extensions
{
    struct CatalogPackage
    {
        std::filesystem::path packageRoot;
        std::filesystem::path manifestPath;
        std::filesystem::path entryPath;
        nlohmann::json manifest;
        std::string manifestText;
        std::string extensionId;
        std::string version;
        std::string integrityStatus = "notDeclared";
        std::vector<Diagnostic> diagnostics;
    };

    struct CatalogScan
    {
        std::filesystem::path root;
        std::vector<CatalogPackage> packages;
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] nlohmann::json ToJson(
            std::string status,
            std::uint64_t generation,
            const nlohmann::json& activeExtensions) const;
    };

    // Discovery is intentionally side-effect free: it never loads extension code.
    // This boundary makes validation suitable for installers, CI, and CLI diagnostics.
    class ExtensionCatalog final
    {
    public:
        [[nodiscard]] CatalogScan Discover(
            const std::filesystem::path& approvedRoot) const;
    };
}

#include "ExtensionCatalog.h"

#include "../../ARTest.SDK/include/ARTestExtensionAbi.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

namespace
{
    constexpr std::uintmax_t MaximumManifestSize = 1024U * 1024U;
    constexpr const char* ManifestName = "artest-extension.json";

    [[nodiscard]] std::string StringValue(
        const nlohmann::json& object,
        const char* name)
    {
        const auto found = object.find(name);
        return found != object.end() && found->is_string()
            ? found->get<std::string>() : std::string{};
    }

    [[nodiscard]] std::uint32_t UnsignedValue(
        const nlohmann::json& object,
        const char* name,
        std::uint32_t fallback)
    {
        const auto found = object.find(name);
        if (found == object.end() || !found->is_number_unsigned()) return fallback;
        const auto value = found->get<std::uint64_t>();
        return value <= UINT32_MAX ? static_cast<std::uint32_t>(value) : fallback;
    }

    [[nodiscard]] bool HasOnlyProperties(
        const nlohmann::json& object,
        std::initializer_list<const char*> allowed)
    {
        std::set<std::string> names;
        for (const auto* name : allowed) names.emplace(name);
        for (auto item = object.cbegin(); item != object.cend(); ++item)
            if (!names.contains(item.key())) return false;
        return true;
    }

    [[nodiscard]] std::string SeverityName(artest::DiagnosticSeverity severity)
    {
        switch (severity)
        {
        case artest::DiagnosticSeverity::Information: return "information";
        case artest::DiagnosticSeverity::Warning: return "warning";
        case artest::DiagnosticSeverity::Error: return "error";
        }
        return "error";
    }

    [[nodiscard]] nlohmann::json DiagnosticJson(const artest::Diagnostic& value)
    {
        return {
            {"severity", SeverityName(value.severity)},
            {"code", value.code},
            {"message", value.message},
            {"location", value.location}};
    }

    [[nodiscard]] bool IsStableId(const std::string& value)
    {
        static const std::regex expression{
            R"(^[a-z0-9]+(?:[.-][a-z0-9]+(?:-[a-z0-9]+)*)+$)"};
        return std::regex_match(value, expression);
    }

    [[nodiscard]] bool IsSemanticVersion(const std::string& value)
    {
        static const std::regex expression{
            R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$)"};
        return std::regex_match(value, expression);
    }

    [[nodiscard]] bool IsContained(
        const std::filesystem::path& root,
        const std::filesystem::path& candidate)
    {
        const auto relative = candidate.lexically_relative(root);
        return !relative.empty()
            && !relative.is_absolute()
            && relative.begin() != relative.end()
            && *relative.begin() != std::filesystem::path{".."};
    }

    [[nodiscard]] bool IsHexSha256(const std::string& value)
    {
        return value.size() == 64U
            && std::all_of(value.begin(), value.end(), [](unsigned char character)
            {
                return std::isxdigit(character) != 0;
            });
    }

    [[nodiscard]] std::string Sha256(const std::filesystem::path& path)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectSize = 0U;
        DWORD bytesWritten = 0U;
        std::vector<unsigned char> object;
        std::array<unsigned char, 32U> digest{};

        const auto cleanup = [&]() noexcept
        {
            if (hash != nullptr) BCryptDestroyHash(hash);
            if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0U);
        };

        if (BCryptOpenAlgorithmProvider(
                &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0U) < 0
            || BCryptGetProperty(
                algorithm, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                &bytesWritten, 0U) < 0)
        {
            cleanup();
            throw std::runtime_error("Windows could not initialize SHA-256.");
        }
        object.resize(objectSize);
        if (BCryptCreateHash(
                algorithm, &hash, object.data(), objectSize,
                nullptr, 0U, 0U) < 0)
        {
            cleanup();
            throw std::runtime_error("Windows could not create a SHA-256 hash.");
        }

        std::ifstream input{path, std::ios::binary};
        if (!input)
        {
            cleanup();
            throw std::runtime_error("The extension entry could not be opened for hashing.");
        }
        std::array<char, 64U * 1024U> buffer{};
        while (input)
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0 && BCryptHashData(
                    hash, reinterpret_cast<PUCHAR>(buffer.data()),
                    static_cast<ULONG>(count), 0U) < 0)
            {
                cleanup();
                throw std::runtime_error("Windows failed while hashing the extension entry.");
            }
        }
        if (input.bad()
            || BCryptFinishHash(hash, digest.data(),
                static_cast<ULONG>(digest.size()), 0U) < 0)
        {
            cleanup();
            throw std::runtime_error("The extension entry SHA-256 could not be completed.");
        }
        cleanup();

        std::ostringstream text;
        text << std::hex << std::setfill('0');
        for (const auto byte : digest)
            text << std::setw(2) << static_cast<unsigned int>(byte);
        return text.str();
    }

    void AddPackageDiagnostic(
        artest::extensions::CatalogPackage& package,
        std::string code,
        std::string message,
        std::string location = {})
    {
        package.diagnostics.push_back({
            artest::DiagnosticSeverity::Error,
            std::move(code),
            std::move(message),
            location.empty() ? package.manifestPath.string() : std::move(location)});
    }

    void ValidateComponent(
        const nlohmann::json& component,
        artest::extensions::CatalogPackage& package,
        std::set<std::string>& packageTypes)
    {
        if (!component.is_object())
        {
            AddPackageDiagnostic(package, "EXTENSION_COMPONENT_INVALID",
                "Every component declaration must be a JSON object.");
            return;
        }

        if (!HasOnlyProperties(component, {"kind", "typeId", "contractId",
            "version", "displayName", "description", "capabilities", "requires",
            "schemas", "flags"}))
            AddPackageDiagnostic(package, "EXTENSION_COMPONENT_PROPERTY_UNKNOWN",
                "A component contains a property outside the manifest schema.");

        const auto kind = StringValue(component, "kind");
        const auto typeId = StringValue(component, "typeId");
        const auto contractId = StringValue(component, "contractId");
        const auto version = StringValue(component, "version");
        if (kind != "command" && kind != "instrumentDriver" && kind != "tool")
            AddPackageDiagnostic(package, "EXTENSION_COMPONENT_KIND_INVALID",
                "Component kind must be command, instrumentDriver, or tool.");
        if (!IsStableId(typeId))
            AddPackageDiagnostic(package, "EXTENSION_COMPONENT_ID_INVALID",
                "Component typeId must be a lower-case stable identifier.");
        else if (!packageTypes.emplace(typeId).second)
            AddPackageDiagnostic(package, "EXTENSION_COMPONENT_DUPLICATE",
                "A component typeId is declared more than once in the package.", typeId);
        if (!IsStableId(contractId))
            AddPackageDiagnostic(package, "EXTENSION_CONTRACT_ID_INVALID",
                "Component contractId must be a lower-case stable identifier.");
        if (!IsSemanticVersion(version))
            AddPackageDiagnostic(package, "EXTENSION_COMPONENT_VERSION_INVALID",
                "Component version must use semantic versioning.", typeId);
        if (StringValue(component, "displayName").empty())
            AddPackageDiagnostic(package, "EXTENSION_COMPONENT_DISPLAY_NAME_INVALID",
                "Component displayName must be a non-empty string.", typeId);

        if (component.contains("schemas"))
        {
            if (!component["schemas"].is_array())
            {
                AddPackageDiagnostic(package, "EXTENSION_SCHEMAS_INVALID",
                    "Component schemas must be an array.", typeId);
            }
            else
            {
                for (const auto& binding : component["schemas"])
                {
                    if (!binding.is_object()
                        || !HasOnlyProperties(binding,
                            {"role", "schemaId", "path", "mediaType"})
                        || StringValue(binding, "role").empty()
                        || !IsStableId(StringValue(binding, "schemaId"))
                        || StringValue(binding, "mediaType").empty())
                    {
                        AddPackageDiagnostic(package, "EXTENSION_SCHEMA_BINDING_INVALID",
                            "A schema binding does not match the manifest contract.", typeId);
                        continue;
                    }
                    const auto relative = std::filesystem::path{
                        StringValue(binding, "path")};
                    const auto path = std::filesystem::weakly_canonical(
                        package.packageRoot / relative);
                    if (relative.empty() || relative.is_absolute()
                        || !IsContained(package.packageRoot, path)
                        || !std::filesystem::is_regular_file(path))
                    {
                        AddPackageDiagnostic(package, "EXTENSION_SCHEMA_PATH_INVALID",
                            "A declared schema must be an existing file inside its package.",
                            path.string());
                    }
                }
            }
        }
    }
}

namespace artest::extensions
{
    bool CatalogScan::IsValid() const noexcept
    {
        if (ContainsErrors(diagnostics)) return false;
        return std::none_of(packages.begin(), packages.end(),
            [](const CatalogPackage& package)
            {
                return ContainsErrors(package.diagnostics);
            });
    }

    nlohmann::json CatalogScan::ToJson(
        std::string status,
        std::uint64_t generation,
        const nlohmann::json& activeExtensions) const
    {
        nlohmann::json value{
            {"schema", "artest.schema.extension-catalog.v2"},
            {"status", std::move(status)},
            {"valid", IsValid()},
            {"generation", generation},
            {"root", root.string()},
            {"abi", {
                {"major", ARTEST_EXTENSION_ABI_MAJOR},
                {"minor", ARTEST_EXTENSION_ABI_MINOR}}},
            {"packages", nlohmann::json::array()},
            {"extensions", activeExtensions},
            {"diagnostics", nlohmann::json::array()}};

        for (const auto& diagnostic : diagnostics)
            value["diagnostics"].push_back(DiagnosticJson(diagnostic));
        for (const auto& package : packages)
        {
            nlohmann::json item{
                {"extensionId", package.extensionId},
                {"version", package.version},
                {"packageRoot", package.packageRoot.string()},
                {"manifestPath", package.manifestPath.string()},
                {"entryPath", package.entryPath.string()},
                {"integrity", package.integrityStatus},
                {"valid", !ContainsErrors(package.diagnostics)},
                {"diagnostics", nlohmann::json::array()}};
            for (const auto& diagnostic : package.diagnostics)
            {
                item["diagnostics"].push_back(DiagnosticJson(diagnostic));
                value["diagnostics"].push_back(DiagnosticJson(diagnostic));
            }
            value["packages"].push_back(std::move(item));
        }
        return value;
    }

    CatalogScan ExtensionCatalog::Discover(
        const std::filesystem::path& approvedRoot) const
    {
        CatalogScan scan;
        try
        {
            scan.root = std::filesystem::weakly_canonical(approvedRoot);
            if (!std::filesystem::is_directory(scan.root))
            {
                scan.diagnostics.push_back({DiagnosticSeverity::Error,
                    "EXTENSION_ROOT_INVALID",
                    "The approved extension root is not a directory.",
                    scan.root.string()});
                return scan;
            }

            std::vector<std::filesystem::path> packageRoots;
            for (const auto& entry : std::filesystem::directory_iterator(scan.root))
                if (entry.is_directory()) packageRoots.push_back(entry.path());
            std::sort(packageRoots.begin(), packageRoots.end());

            std::unordered_map<std::string, std::size_t> extensionOwners;
            std::unordered_map<std::string, std::size_t> typeOwners;
            for (const auto& packagePath : packageRoots)
            {
                const auto manifestPath = packagePath / ManifestName;
                if (!std::filesystem::is_regular_file(manifestPath)) continue;

                CatalogPackage package;
                package.packageRoot = std::filesystem::weakly_canonical(packagePath);
                package.manifestPath = manifestPath;
                scan.packages.push_back(std::move(package));
                auto& current = scan.packages.back();

                if (std::filesystem::file_size(manifestPath) > MaximumManifestSize)
                {
                    AddPackageDiagnostic(current, "EXTENSION_MANIFEST_TOO_LARGE",
                        "The manifest exceeds the 1 MB limit.");
                    continue;
                }

                std::ifstream input{manifestPath, std::ios::binary};
                std::ostringstream buffer;
                buffer << input.rdbuf();
                current.manifestText = std::move(buffer).str();
                try
                {
                    current.manifest = nlohmann::json::parse(current.manifestText);
                }
                catch (const std::exception& exception)
                {
                    AddPackageDiagnostic(current, "EXTENSION_MANIFEST_JSON_INVALID",
                        exception.what());
                    continue;
                }
                if (!current.manifest.is_object())
                {
                    AddPackageDiagnostic(current, "EXTENSION_MANIFEST_INVALID",
                        "The extension manifest must be a JSON object.");
                    continue;
                }

                const auto& manifest = current.manifest;
                if (!HasOnlyProperties(manifest, {"schemaVersion", "extensionId",
                    "displayName", "version", "publisher", "description", "runtime",
                    "components", "integrity"}))
                    AddPackageDiagnostic(current, "EXTENSION_MANIFEST_PROPERTY_UNKNOWN",
                        "The manifest contains a property outside schemaVersion 1.");
                current.extensionId = StringValue(manifest, "extensionId");
                current.version = StringValue(manifest, "version");
                if (UnsignedValue(manifest, "schemaVersion", UINT32_MAX) != 1U)
                    AddPackageDiagnostic(current, "EXTENSION_SCHEMA_VERSION_UNSUPPORTED",
                        "Only extension manifest schemaVersion 1 is supported.");
                if (!IsStableId(current.extensionId))
                    AddPackageDiagnostic(current, "EXTENSION_ID_INVALID",
                        "extensionId must be a lower-case reverse-domain identifier.");
                else
                {
                    const auto [owner, inserted] = extensionOwners.emplace(
                        current.extensionId, scan.packages.size() - 1U);
                    if (!inserted)
                    {
                        AddPackageDiagnostic(current, "EXTENSION_ID_DUPLICATE",
                            "The extensionId is already declared by another package.",
                            current.extensionId);
                        AddPackageDiagnostic(scan.packages[owner->second],
                            "EXTENSION_ID_DUPLICATE",
                            "The extensionId is also declared by another package.",
                            current.extensionId);
                    }
                }
                if (!IsSemanticVersion(current.version))
                    AddPackageDiagnostic(current, "EXTENSION_VERSION_INVALID",
                        "Extension version must use semantic versioning.");
                if (StringValue(manifest, "displayName").empty())
                    AddPackageDiagnostic(current, "EXTENSION_DISPLAY_NAME_INVALID",
                        "displayName must be a non-empty string.");
                if (StringValue(manifest, "publisher").empty())
                    AddPackageDiagnostic(current, "EXTENSION_PUBLISHER_INVALID",
                        "publisher must be a non-empty string.");

                if (!manifest.contains("runtime") || !manifest["runtime"].is_object())
                {
                    AddPackageDiagnostic(current, "EXTENSION_RUNTIME_INVALID",
                        "A runtime object is required.");
                }
                else
                {
                    const auto& runtime = manifest["runtime"];
                    if (!HasOnlyProperties(runtime, {"kind", "entry", "entryPoint",
                        "isolation", "architecture", "abi", "protocol", "python",
                        "targetFramework"}))
                        AddPackageDiagnostic(current, "EXTENSION_RUNTIME_PROPERTY_UNKNOWN",
                            "The runtime contains a property outside the manifest schema.");
                    if (StringValue(runtime, "kind") != "native"
                        || StringValue(runtime, "isolation") != "inProcess"
                        || StringValue(runtime, "architecture") != "x64"
                        || !runtime.contains("abi") || !runtime["abi"].is_object()
                        || UnsignedValue(runtime["abi"], "major", UINT32_MAX)
                            != ARTEST_EXTENSION_ABI_MAJOR
                        || UnsignedValue(runtime["abi"], "minor", UINT32_MAX)
                            > ARTEST_EXTENSION_ABI_MINOR)
                    {
                        AddPackageDiagnostic(current, "EXTENSION_RUNTIME_INCOMPATIBLE",
                            "The native runtime, architecture, isolation, or ABI is incompatible.");
                    }

                    const auto entry = StringValue(runtime, "entry");
                    const auto entryRelative = std::filesystem::path{entry};
                    const auto entryPath = std::filesystem::weakly_canonical(
                        current.packageRoot / entryRelative);
                    current.entryPath = entryPath;
                    if (entry.empty() || entryRelative.is_absolute()
                        || !IsContained(current.packageRoot, entryPath)
                        || !std::filesystem::is_regular_file(entryPath))
                    {
                        AddPackageDiagnostic(current, "EXTENSION_ENTRY_INVALID",
                            "The runtime entry must be an existing file inside its package.");
                    }
                }

                if (!manifest.contains("components")
                    || !manifest["components"].is_array()
                    || manifest["components"].empty())
                {
                    AddPackageDiagnostic(current, "EXTENSION_COMPONENTS_INVALID",
                        "At least one component declaration is required.");
                }
                else
                {
                    std::set<std::string> packageTypes;
                    for (const auto& component : manifest["components"])
                    {
                        ValidateComponent(component, current, packageTypes);
                        if (!component.is_object()) continue;
                        const auto typeId = StringValue(component, "typeId");
                        if (!IsStableId(typeId)) continue;
                        const auto [owner, inserted] = typeOwners.emplace(
                            typeId, scan.packages.size() - 1U);
                        if (!inserted && owner->second != scan.packages.size() - 1U)
                        {
                            AddPackageDiagnostic(current, "EXTENSION_COMPONENT_DUPLICATE",
                                "The component typeId is declared by another package.", typeId);
                            AddPackageDiagnostic(scan.packages[owner->second],
                                "EXTENSION_COMPONENT_DUPLICATE",
                                "The component typeId is also declared by another package.", typeId);
                        }
                    }
                }

                if (manifest.contains("integrity"))
                {
                    const auto& integrity = manifest["integrity"];
                    if (!integrity.is_object())
                    {
                        current.integrityStatus = "invalid";
                        AddPackageDiagnostic(current, "EXTENSION_INTEGRITY_INVALID",
                            "integrity must be a JSON object.");
                        continue;
                    }
                    if (!HasOnlyProperties(integrity,
                        {"sha256", "signature", "publisherCertificate"}))
                        AddPackageDiagnostic(current, "EXTENSION_INTEGRITY_PROPERTY_UNKNOWN",
                            "The integrity object contains an unsupported property.");
                    if (integrity.contains("signature")
                        || integrity.contains("publisherCertificate"))
                    {
                        current.diagnostics.push_back({DiagnosticSeverity::Warning,
                            "EXTENSION_SIGNATURE_NOT_ENFORCED",
                            "Publisher signature trust is reserved and is not enforced in D3.1.",
                            current.manifestPath.string()});
                    }
                    if (integrity.contains("sha256"))
                    {
                        const auto expected = StringValue(integrity, "sha256");
                        if (!IsHexSha256(expected))
                        {
                            current.integrityStatus = "invalid";
                            AddPackageDiagnostic(current, "EXTENSION_INTEGRITY_INVALID",
                                "integrity.sha256 must contain 64 hexadecimal characters.");
                        }
                        else if (std::filesystem::is_regular_file(current.entryPath))
                        {
                            auto normalized = expected;
                            std::transform(normalized.begin(), normalized.end(),
                                normalized.begin(), [](unsigned char character)
                                {
                                    return static_cast<char>(std::tolower(character));
                                });
                            if (Sha256(current.entryPath) != normalized)
                            {
                                current.integrityStatus = "mismatch";
                                AddPackageDiagnostic(current, "EXTENSION_INTEGRITY_MISMATCH",
                                    "The runtime entry SHA-256 does not match the manifest.",
                                    current.entryPath.string());
                            }
                            else
                            {
                                current.integrityStatus = "verified";
                            }
                        }
                    }
                }
            }

            if (scan.packages.empty())
                scan.diagnostics.push_back({DiagnosticSeverity::Error,
                    "EXTENSION_CATALOG_EMPTY",
                    "No extension packages were found in the approved root.",
                    scan.root.string()});
        }
        catch (const std::exception& exception)
        {
            scan.diagnostics.push_back({DiagnosticSeverity::Error,
                "EXTENSION_CATALOG_EXCEPTION", exception.what(),
                approvedRoot.string()});
        }
        return scan;
    }
}

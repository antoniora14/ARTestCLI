#include "../../ARTest.SDK/include/ARTestExtensionAbi.h"
#include "../../ARTestEngine.Core/Catalog/ComponentCatalog.h"
#include "CatalogValidation.h"
#include "FileIntegrity.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
namespace artest::extensions
{
CatalogScan ExtensionCatalog::Discover(const std::filesystem::path &approvedRoot) const
{
    CatalogScan scan;
    try
    {
        scan.root = std::filesystem::weakly_canonical(approvedRoot);
        if (!std::filesystem::is_directory(scan.root))
        {
            scan.diagnostics.push_back({DiagnosticSeverity::Error, "EXTENSION_ROOT_INVALID",
                                        "The approved extension root is not a directory.",
                                        scan.root.string()});
            return scan;
        }

        std::vector<std::filesystem::path> packageRoots;
        for (const auto &entry : std::filesystem::directory_iterator(scan.root))
            if (entry.is_directory())
                packageRoots.push_back(entry.path());
        std::sort(packageRoots.begin(), packageRoots.end());

        std::unordered_map<std::string, std::size_t> extensionOwners;
        std::unordered_map<std::string, std::size_t> typeOwners;
        for (const auto &packagePath : packageRoots)
        {
            const auto manifestPath = packagePath / ManifestName;
            if (!std::filesystem::is_regular_file(manifestPath))
                continue;

            CatalogPackage package;
            package.packageRoot = std::filesystem::weakly_canonical(packagePath);
            package.manifestPath = manifestPath;
            scan.packages.push_back(std::move(package));
            auto &current = scan.packages.back();

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
            catch (const std::exception &exception)
            {
                AddPackageDiagnostic(current, "EXTENSION_MANIFEST_JSON_INVALID", exception.what());
                continue;
            }
            if (!current.manifest.is_object())
            {
                AddPackageDiagnostic(current, "EXTENSION_MANIFEST_INVALID",
                                     "The extension manifest must be a JSON object.");
                continue;
            }

            const auto &manifest = current.manifest;
            if (!HasOnlyProperties(manifest, {"schemaVersion", "extensionId", "displayName",
                                              "version", "publisher", "description", "runtime",
                                              "components", "integrity"}))
                AddPackageDiagnostic(current, "EXTENSION_MANIFEST_PROPERTY_UNKNOWN",
                                     "The manifest contains a property outside schemaVersion 1.");
            current.extensionId = StringValue(manifest, "extensionId");
            current.version = StringValue(manifest, "version");
            if (UnsignedValue(manifest, "schemaVersion", UINT32_MAX) != 1U &&
                UnsignedValue(manifest, "schemaVersion", UINT32_MAX) != 2U)
                AddPackageDiagnostic(current, "EXTENSION_SCHEMA_VERSION_UNSUPPORTED",
                                     "Only extension manifest schemaVersion 1 or 2 is supported.");
            if (!IsStableId(current.extensionId))
                AddPackageDiagnostic(current, "EXTENSION_ID_INVALID",
                                     "extensionId must be a lower-case reverse-domain identifier.");
            else
            {
                const auto [owner, inserted] =
                    extensionOwners.emplace(current.extensionId, scan.packages.size() - 1U);
                if (!inserted)
                {
                    AddPackageDiagnostic(current, "EXTENSION_ID_DUPLICATE",
                                         "The extensionId is already declared by another package.",
                                         current.extensionId);
                    AddPackageDiagnostic(scan.packages[owner->second], "EXTENSION_ID_DUPLICATE",
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
            current.descriptor.extensionId = current.extensionId;
            current.descriptor.version = current.version;
            current.descriptor.displayName = StringValue(manifest, "displayName");
            current.descriptor.publisher = StringValue(manifest, "publisher");

            if (!manifest.contains("runtime") || !manifest["runtime"].is_object())
            {
                AddPackageDiagnostic(current, "EXTENSION_RUNTIME_INVALID",
                                     "A runtime object is required.");
            }
            else
            {
                const auto &runtime = manifest["runtime"];
                if (!HasOnlyProperties(runtime,
                                       {"kind", "entry", "entryPoint", "isolation", "architecture",
                                        "abi", "protocol", "python", "targetFramework"}))
                    AddPackageDiagnostic(
                        current, "EXTENSION_RUNTIME_PROPERTY_UNKNOWN",
                        "The runtime contains a property outside the manifest schema.");
                if (StringValue(runtime, "kind") != "native" ||
                    StringValue(runtime, "isolation") != "inProcess" ||
                    StringValue(runtime, "architecture") != "x64" || !runtime.contains("abi") ||
                    !runtime["abi"].is_object() ||
                    UnsignedValue(runtime["abi"], "major", UINT32_MAX) !=
                        ARTEST_EXTENSION_ABI_MAJOR ||
                    UnsignedValue(runtime["abi"], "minor", UINT32_MAX) > ARTEST_EXTENSION_ABI_MINOR)
                {
                    AddPackageDiagnostic(
                        current, "EXTENSION_RUNTIME_INCOMPATIBLE",
                        "The native runtime, architecture, isolation, or ABI is incompatible.");
                }

                const auto entry = StringValue(runtime, "entry");
                current.descriptor.runtime = {
                    StringValue(runtime, "kind"),
                    entry,
                    StringValue(runtime, "isolation"),
                    StringValue(runtime, "architecture"),
                    runtime.contains("abi") && runtime["abi"].is_object()
                        ? UnsignedValue(runtime["abi"], "major", UINT32_MAX)
                        : UINT32_MAX,
                    runtime.contains("abi") && runtime["abi"].is_object()
                        ? UnsignedValue(runtime["abi"], "minor", UINT32_MAX)
                        : UINT32_MAX};
                const auto entryRelative = std::filesystem::path{entry};
                const auto entryPath =
                    std::filesystem::weakly_canonical(current.packageRoot / entryRelative);
                current.entryPath = entryPath;
                if (entry.empty() || entryRelative.is_absolute() ||
                    !IsContained(current.packageRoot, entryPath) ||
                    !std::filesystem::is_regular_file(entryPath))
                {
                    AddPackageDiagnostic(
                        current, "EXTENSION_ENTRY_INVALID",
                        "The runtime entry must be an existing file inside its package.");
                }
            }

            if (!manifest.contains("components") || !manifest["components"].is_array() ||
                manifest["components"].empty())
            {
                AddPackageDiagnostic(current, "EXTENSION_COMPONENTS_INVALID",
                                     "At least one component declaration is required.");
            }
            else
            {
                std::set<std::string> packageTypes;
                for (const auto &component : manifest["components"])
                {
                    ValidateComponent(component, current, packageTypes);
                    if (!component.is_object())
                        continue;
                    const auto typeId = StringValue(component, "typeId");
                    if (!IsStableId(typeId))
                        continue;
                    const auto [owner, inserted] =
                        typeOwners.emplace(typeId, scan.packages.size() - 1U);
                    if (!inserted && owner->second != scan.packages.size() - 1U)
                    {
                        AddPackageDiagnostic(current, "EXTENSION_COMPONENT_DUPLICATE",
                                             "The component typeId is declared by another package.",
                                             typeId);
                        AddPackageDiagnostic(
                            scan.packages[owner->second], "EXTENSION_COMPONENT_DUPLICATE",
                            "The component typeId is also declared by another package.", typeId);
                    }
                }
            }

            if (manifest.contains("integrity"))
            {
                const auto &integrity = manifest["integrity"];
                if (!integrity.is_object())
                {
                    current.integrityStatus = "invalid";
                    AddPackageDiagnostic(current, "EXTENSION_INTEGRITY_INVALID",
                                         "integrity must be a JSON object.");
                    continue;
                }
                if (!HasOnlyProperties(integrity, {"sha256", "signature", "publisherCertificate"}))
                    AddPackageDiagnostic(current, "EXTENSION_INTEGRITY_PROPERTY_UNKNOWN",
                                         "The integrity object contains an unsupported property.");
                if (integrity.contains("signature") || integrity.contains("publisherCertificate"))
                {
                    current.descriptor.integrity.signatureDeclared = true;
                    current.diagnostics.push_back(
                        {DiagnosticSeverity::Warning, "EXTENSION_SIGNATURE_NOT_ENFORCED",
                         "Publisher signature trust is reserved and is not enforced in D3.1.",
                         current.manifestPath.string()});
                }
                if (integrity.contains("sha256"))
                {
                    const auto expected = StringValue(integrity, "sha256");
                    current.descriptor.integrity.declaredSha256 = expected;
                    if (!IsHexSha256(expected))
                    {
                        current.integrityStatus = "invalid";
                        AddPackageDiagnostic(
                            current, "EXTENSION_INTEGRITY_INVALID",
                            "integrity.sha256 must contain 64 hexadecimal characters.");
                    }
                    else if (std::filesystem::is_regular_file(current.entryPath))
                    {
                        auto normalized = expected;
                        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                                       [](unsigned char character) {
                                           return static_cast<char>(std::tolower(character));
                                       });
                        if (Sha256(current.entryPath) != normalized)
                        {
                            current.integrityStatus = "mismatch";
                            AddPackageDiagnostic(
                                current, "EXTENSION_INTEGRITY_MISMATCH",
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
            if (std::filesystem::is_regular_file(current.entryPath))
                current.descriptor.integrity.contentSha256 = Sha256(current.entryPath);
        }

        artest::ComponentCatalog identities;
        const auto unique = identities.Add(scan.Components());
        scan.diagnostics.insert(scan.diagnostics.end(), unique.diagnostics.begin(),
                                unique.diagnostics.end());

        if (scan.packages.empty())
            scan.diagnostics.push_back({DiagnosticSeverity::Error, "EXTENSION_CATALOG_EMPTY",
                                        "No extension packages were found in the approved root.",
                                        scan.root.string()});
    }
    catch (const std::exception &exception)
    {
        scan.diagnostics.push_back({DiagnosticSeverity::Error, "EXTENSION_CATALOG_EXCEPTION",
                                    exception.what(), approvedRoot.string()});
    }
    return scan;
}
} // namespace artest::extensions

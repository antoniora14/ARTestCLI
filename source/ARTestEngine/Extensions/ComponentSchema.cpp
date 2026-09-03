#include "../../ARTestEngine.Core/Catalog/SchemaValidator.h"
#include "CatalogValidation.h"
#include <fstream>
namespace artest::extensions
{
void ValidateComponent(const nlohmann::json &component, artest::extensions::CatalogPackage &package,
                       std::set<std::string> &packageTypes)
{
    if (!component.is_object())
    {
        AddPackageDiagnostic(package, "EXTENSION_COMPONENT_INVALID",
                             "Every component declaration must be a JSON object.");
        return;
    }

    if (!HasOnlyProperties(component,
                           {"kind", "typeId", "contractId", "version", "displayName", "description",
                            "capabilities", "requires", "schemas", "flags", "aliases"}))
        AddPackageDiagnostic(package, "EXTENSION_COMPONENT_PROPERTY_UNKNOWN",
                             "A component contains a property outside the manifest schema.");

    const auto kind = StringValue(component, "kind");
    const auto typeId = StringValue(component, "typeId");
    const auto contractId = StringValue(component, "contractId");
    const auto version = StringValue(component, "version");
    artest::ComponentDescriptor typed;
    typed.kind = kind == "command"            ? artest::ComponentKind::Command
                 : kind == "instrumentDriver" ? artest::ComponentKind::InstrumentDriver
                                              : artest::ComponentKind::Tool;
    typed.typeId = typeId;
    typed.contractId = contractId;
    typed.version = version;
    typed.displayName = StringValue(component, "displayName");
    const auto readStrings = [&](const char *key, std::vector<std::string> &target) {
        if (!component.contains(key))
            return;
        if (!component[key].is_array())
        {
            AddPackageDiagnostic(package, "EXTENSION_COMPONENT_INVALID",
                                 std::string{key} + " must be an array.", typeId);
            return;
        }
        std::set<std::string> unique;
        for (const auto &value : component[key])
        {
            if (!value.is_string() || value.get<std::string>().empty() ||
                !unique.insert(value.get<std::string>()).second)
                AddPackageDiagnostic(package, "EXTENSION_COMPONENT_INVALID",
                                     std::string{key} + " must contain unique non-empty strings.",
                                     typeId);
            else
                target.push_back(value.get<std::string>());
        }
    };
    readStrings("aliases", typed.aliases);
    readStrings("capabilities", typed.capabilities);
    readStrings("flags", typed.flags);
    for (const auto &flag : typed.flags)
        if (flag != "simulated" && flag != "requiresHardware")
            AddPackageDiagnostic(package, "EXTENSION_FLAG_INVALID", "Unsupported component flag.",
                                 typeId);
    if (!typed.aliases.empty() && UnsignedValue(package.manifest, "schemaVersion", 0U) < 2U)
        AddPackageDiagnostic(package, "EXTENSION_ALIAS_VERSION_INVALID",
                             "Aliases require manifest schemaVersion 2.", typeId);
    for (const auto &capability : typed.capabilities)
        if (!IsStableId(capability))
            AddPackageDiagnostic(package, "EXTENSION_CAPABILITY_INVALID",
                                 "Capability IDs must be stable IDs.", typeId);
    if (component.contains("requires"))
    {
        if (!component["requires"].is_array())
            AddPackageDiagnostic(package, "EXTENSION_REQUIREMENT_INVALID",
                                 "requires must be an array.", typeId);
        else
            for (const auto &requirement : component["requires"])
            {
                if (!requirement.is_object() ||
                    !HasOnlyProperties(requirement, {"contractId", "selection"}) ||
                    !IsStableId(StringValue(requirement, "contractId")) ||
                    StringValue(requirement, "selection") != "configured")
                    AddPackageDiagnostic(package, "EXTENSION_REQUIREMENT_INVALID",
                                         "Only configured contract requirements are supported.",
                                         typeId);
                else
                    typed.requiredContracts.push_back(StringValue(requirement, "contractId"));
            }
    }
    if (kind != "command" && kind != "instrumentDriver" && kind != "tool")
        AddPackageDiagnostic(package, "EXTENSION_COMPONENT_KIND_INVALID",
                             "Component kind must be command, instrumentDriver, or tool.");
    if (!IsStableId(typeId))
        AddPackageDiagnostic(package, "EXTENSION_COMPONENT_ID_INVALID",
                             "Component typeId must be a lower-case stable identifier.");
    else if (!packageTypes.emplace(typeId).second)
        AddPackageDiagnostic(package, "EXTENSION_COMPONENT_DUPLICATE",
                             "A component typeId is declared more than once in the package.",
                             typeId);
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
            std::set<std::string> roles;
            for (const auto &binding : component["schemas"])
            {
                if (!binding.is_object() ||
                    !HasOnlyProperties(binding, {"role", "schemaId", "path", "mediaType"}) ||
                    StringValue(binding, "role").empty() ||
                    !IsStableId(StringValue(binding, "schemaId")) ||
                    StringValue(binding, "mediaType").empty())
                {
                    AddPackageDiagnostic(package, "EXTENSION_SCHEMA_BINDING_INVALID",
                                         "A schema binding does not match the manifest contract.",
                                         typeId);
                    continue;
                }
                const auto relative = std::filesystem::path{StringValue(binding, "path")};
                const auto path = std::filesystem::weakly_canonical(package.packageRoot / relative);
                if (relative.empty() || relative.is_absolute() ||
                    !IsContained(package.packageRoot, path) ||
                    !std::filesystem::is_regular_file(path))
                {
                    AddPackageDiagnostic(
                        package, "EXTENSION_SCHEMA_PATH_INVALID",
                        "A declared schema must be an existing file inside its package.",
                        path.string());
                    continue;
                }
                const auto role = StringValue(binding, "role");
                if (!roles.insert(role).second ||
                    (role != "parameters" && role != "configuration" && role != "result") ||
                    StringValue(binding, "mediaType") != "application/json; charset=utf-8")
                {
                    AddPackageDiagnostic(package, "EXTENSION_SCHEMA_BINDING_INVALID",
                                         "Schema roles must be supported, unique JSON bindings.",
                                         typeId);
                    continue;
                }
                if (std::filesystem::file_size(path) > MaximumManifestSize)
                {
                    AddPackageDiagnostic(package, "EXTENSION_SCHEMA_TOO_LARGE",
                                         "Schema exceeds the 1 MiB limit.", path.string());
                    continue;
                }
                try
                {
                    std::ifstream schemaInput{path};
                    auto document = nlohmann::json::parse(schemaInput);
                    const auto checked = artest::SchemaValidator::Check(document, path.string());
                    package.diagnostics.insert(package.diagnostics.end(),
                                               checked.diagnostics.begin(),
                                               checked.diagnostics.end());
                    typed.schemas.push_back({role, StringValue(binding, "schemaId"),
                                             StringValue(binding, "mediaType"),
                                             relative.generic_string(), std::move(document)});
                }
                catch (const std::exception &exception)
                {
                    AddPackageDiagnostic(package, "EXTENSION_SCHEMA_JSON_INVALID", exception.what(),
                                         path.string());
                }
            }
        }
    }
    if (UnsignedValue(package.manifest, "schemaVersion", 0U) >= 2U &&
        !typed.Schema(typed.kind == artest::ComponentKind::Command ? "parameters"
                                                                   : "configuration"))
        AddPackageDiagnostic(package, "EXTENSION_SCHEMA_REQUIRED",
                             "Manifest v2 requires the compilation schema.", typeId);
    package.descriptor.components.push_back(std::move(typed));
}
} // namespace artest::extensions

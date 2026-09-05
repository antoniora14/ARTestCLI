#pragma once

#include "Definition.h"
#include "../ARTestExtensionAbi.h"
#include <algorithm>
#include <map>
#include <regex>
#include <set>

namespace artest::sdk
{
// A pure serialization result. Filesystem publication belongs to the build tool,
// not component code or the runtime ABI adapter.
struct MetadataBundle
{
    Json manifest;
    std::map<std::string, Json> schemas;
};

namespace detail
{
inline void MetadataId(const std::string &value)
{
    static const std::regex pattern{"[a-z0-9]+([.-][a-z0-9]+)+"};
    if (value.size() > 160 || !std::regex_match(value, pattern))
        throw std::invalid_argument("Invalid metadata ID: " + value);
}
inline void MetadataVersion(const std::string &value)
{
    static const std::regex pattern{"(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"};
    if (!std::regex_match(value, pattern))
        throw std::invalid_argument("Metadata generation requires a numeric major.minor.patch version.");
}
} // namespace detail

[[nodiscard]] inline MetadataBundle GenerateMetadata(const Extension &extension,
                                                      const std::string &binaryName)
{
    // Runtime and architecture describe this first native x64 slice. They are
    // build concerns, not fields developers must duplicate in every component.
    static const std::regex binaryPattern{"[A-Za-z0-9_-]+\\.dll"};
    if (!std::regex_match(binaryName, binaryPattern))
        throw std::invalid_argument("A simple .dll filename is required.");
    using Access = detail::DefinitionAccess;
    detail::MetadataId(Access::Id(extension));
    detail::MetadataVersion(Access::Version(extension));
    detail::RequireText(Access::Name(extension), "Extension display name");
    detail::RequireText(Access::Publisher(extension), "Publisher");
    if (Access::Components(extension).empty())
        throw std::invalid_argument("Metadata requires at least one component.");

    MetadataBundle output;
    output.manifest = {
        {"schemaVersion", 2}, {"extensionId", Access::Id(extension)},
        {"displayName", Access::Name(extension)}, {"version", Access::Version(extension)},
        {"publisher", Access::Publisher(extension)},
        {"runtime", {{"kind", "native"}, {"entry", binaryName}, {"isolation", "inProcess"},
                     {"architecture", "x64"},
                     {"abi", {{"major", ARTEST_EXTENSION_ABI_MAJOR},
                              {"minor", ARTEST_EXTENSION_ABI_MINOR}}}}},
        {"components", Json::array()}};
    // Sorting gives stable output even when registration order changes.
    std::vector<const detail::Registration *> entries;
    std::set<std::string> identities, schemaIds;
    for (const auto &entry : Access::Components(extension))
    {
        if (!identities.insert(entry.id).second)
            throw std::invalid_argument("Duplicate component identity.");
        entries.push_back(&entry);
    }
    std::sort(entries.begin(), entries.end(), [](const auto *a, const auto *b) { return a->id < b->id; });
    for (const auto *entry : entries)
    {
        detail::MetadataId(entry->id);
        detail::MetadataId(entry->contract);
        detail::MetadataVersion(entry->version);
        const bool driver = entry->driverFactory != nullptr;
        const auto &metadata = entry->metadata;
        if (!metadata.schema)
            throw std::invalid_argument("Missing schema for component: " + entry->id);
        const std::string role = driver ? "configuration" : "parameters";
        const auto schemaId = metadata.schemaId.empty() ? entry->id + "." + role + ".v1" : metadata.schemaId;
        detail::MetadataId(schemaId);
        if (!schemaIds.insert(schemaId).second)
            throw std::invalid_argument("Duplicate schema ID: " + schemaId);
        auto schema = metadata.schema->Document();
        if (schema["type"] != "object")
            throw std::invalid_argument("Component parameters/configuration must be an object.");
        const auto path = "schemas/" + entry->id + "." + role + ".json";
        output.schemas.emplace(path, std::move(schema));
        Json component = {
            {"kind", driver ? "instrumentDriver" : "command"},
            {"typeId", entry->id}, {"contractId", entry->contract}, {"version", entry->version},
            {"displayName", entry->name}, {"capabilities", Json::array({entry->contract})},
            {"flags", Json::array()},
            {"schemas", Json::array({{{"role", role}, {"schemaId", schemaId}, {"path", path},
                                      {"mediaType", "application/json; charset=utf-8"}}})}};
        if (driver) component["flags"].push_back(entry->simulated ? "simulated" : "requiresHardware");
        auto aliases = metadata.aliases;
        std::sort(aliases.begin(), aliases.end());
        for (const auto &alias : aliases)
        {
            detail::RequireText(alias, "Alias");
            if (!identities.insert(alias).second)
                throw std::invalid_argument("Alias collides with a component identity: " + alias);
        }
        if (!aliases.empty()) component["aliases"] = aliases;
        auto contracts = metadata.requiredContracts;
        std::sort(contracts.begin(), contracts.end());
        if (driver && !contracts.empty())
            throw std::invalid_argument("Driver requirements are not supported by this authoring slice.");
        if (std::adjacent_find(contracts.begin(), contracts.end()) != contracts.end())
            throw std::invalid_argument("Duplicate configured service requirement.");
        for (const auto &contract : contracts)
        {
            detail::MetadataId(contract);
            if (!component.contains("requires")) component["requires"] = Json::array();
            component["requires"].push_back({{"contractId", contract}, {"selection", "configured"}});
        }
        output.manifest["components"].push_back(std::move(component));
    }
    // Validate encoding before the caller can publish any files.
    if (output.manifest.dump(2).size() + 1 > 1024 * 1024)
        throw std::invalid_argument("Manifest exceeds the 1 MiB limit.");
    return output;
}
} // namespace artest::sdk

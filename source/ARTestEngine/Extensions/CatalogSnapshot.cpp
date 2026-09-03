#include "../../ARTest.SDK/include/ARTestExtensionAbi.h"
#include "ExtensionCatalog.h"
#include <algorithm>
namespace artest::extensions
{
[[nodiscard]] std::string SeverityName(artest::DiagnosticSeverity severity)
{
    switch (severity)
    {
    case artest::DiagnosticSeverity::Information:
        return "information";
    case artest::DiagnosticSeverity::Warning:
        return "warning";
    case artest::DiagnosticSeverity::Error:
        return "error";
    }
    return "error";
}

[[nodiscard]] nlohmann::json DiagnosticJson(const artest::Diagnostic &value)
{
    return {{"severity", SeverityName(value.severity)},
            {"code", value.code},
            {"message", value.message},
            {"location", value.location}};
}

std::vector<ComponentDescriptor> CatalogScan::Components() const
{
    std::vector<ComponentDescriptor> values;
    for (const auto &package : packages)
        values.insert(values.end(), package.descriptor.components.begin(),
                      package.descriptor.components.end());
    return values;
}

std::string CatalogScan::Fingerprint() const
{
    // Length-prefixed fields avoid ambiguous concatenations. The fingerprint pins
    // executable bytes, metadata and schema contents between compile and activation.
    std::string value;
    const auto append = [&value](const std::string &field) {
        value += std::to_string(field.size()) + ":" + field;
    };
    append(root.generic_string());
    for (const auto &package : packages)
    {
        append(package.manifestText);
        append(package.descriptor.integrity.contentSha256);
        for (const auto &component : package.descriptor.components)
            for (const auto &binding : component.schemas)
                append(binding.document.dump());
    }
    return value;
}

bool CatalogScan::IsValid() const noexcept
{
    if (ContainsErrors(diagnostics))
        return false;
    return std::none_of(packages.begin(), packages.end(), [](const CatalogPackage &package) {
        return ContainsErrors(package.diagnostics);
    });
}

nlohmann::json CatalogScan::ToJson(std::string status, std::uint64_t generation,
                                   const nlohmann::json &activeExtensions) const
{
    nlohmann::json value{
        {"schema", "artest.schema.extension-catalog.v2"},
        {"status", std::move(status)},
        {"valid", IsValid()},
        {"generation", generation},
        {"root", root.string()},
        {"abi", {{"major", ARTEST_EXTENSION_ABI_MAJOR}, {"minor", ARTEST_EXTENSION_ABI_MINOR}}},
        {"packages", nlohmann::json::array()},
        {"extensions", activeExtensions},
        {"diagnostics", nlohmann::json::array()}};

    for (const auto &diagnostic : diagnostics)
        value["diagnostics"].push_back(DiagnosticJson(diagnostic));
    for (const auto &package : packages)
    {
        nlohmann::json item{{"extensionId", package.extensionId},
                            {"version", package.version},
                            {"packageRoot", package.packageRoot.string()},
                            {"manifestPath", package.manifestPath.string()},
                            {"entryPath", package.entryPath.string()},
                            {"integrity", package.integrityStatus},
                            {"valid", !ContainsErrors(package.diagnostics)},
                            {"diagnostics", nlohmann::json::array()}};
        for (const auto &diagnostic : package.diagnostics)
        {
            item["diagnostics"].push_back(DiagnosticJson(diagnostic));
            value["diagnostics"].push_back(DiagnosticJson(diagnostic));
        }
        value["packages"].push_back(std::move(item));
    }
    return value;
}

} // namespace artest::extensions

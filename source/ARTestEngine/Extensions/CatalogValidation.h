#pragma once
#include "ExtensionCatalog.h"
#include <algorithm>
#include <regex>
#include <set>
namespace artest::extensions
{
constexpr std::uintmax_t MaximumManifestSize = 1024U * 1024U;
constexpr const char *ManifestName = "artest-extension.json";

[[nodiscard]] inline std::string StringValue(const nlohmann::json &object, const char *name)
{
    const auto found = object.find(name);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] inline std::uint32_t UnsignedValue(const nlohmann::json &object, const char *name,
                                                 std::uint32_t fallback)
{
    const auto found = object.find(name);
    if (found == object.end() || !found->is_number_unsigned())
        return fallback;
    const auto value = found->get<std::uint64_t>();
    return value <= UINT32_MAX ? static_cast<std::uint32_t>(value) : fallback;
}

[[nodiscard]] inline bool HasOnlyProperties(const nlohmann::json &object,
                                            std::initializer_list<const char *> allowed)
{
    std::set<std::string> names;
    for (const auto *name : allowed)
        names.emplace(name);
    for (auto item = object.cbegin(); item != object.cend(); ++item)
        if (!names.contains(item.key()))
            return false;
    return true;
}

[[nodiscard]] inline bool IsStableId(const std::string &value)
{
    static const std::regex expression{R"(^[a-z0-9]+(?:[.-][a-z0-9]+(?:-[a-z0-9]+)*)+$)"};
    return std::regex_match(value, expression);
}

[[nodiscard]] inline bool IsSemanticVersion(const std::string &value)
{
    static const std::regex expression{
        R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$)"};
    return std::regex_match(value, expression);
}

[[nodiscard]] inline bool IsContained(const std::filesystem::path &root,
                                      const std::filesystem::path &candidate)
{
    const auto relative = candidate.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && relative.begin() != relative.end() &&
           *relative.begin() != std::filesystem::path{".."};
}

[[nodiscard]] inline bool IsHexSha256(const std::string &value)
{
    return value.size() == 64U &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char character) { return std::isxdigit(character) != 0; });
}

inline void AddPackageDiagnostic(artest::extensions::CatalogPackage &package, std::string code,
                                 std::string message, std::string location = {})
{
    package.diagnostics.push_back(
        {artest::DiagnosticSeverity::Error, std::move(code), std::move(message),
         location.empty() ? package.manifestPath.string() : std::move(location)});
}

void ValidateComponent(const nlohmann::json &component, CatalogPackage &package,
                       std::set<std::string> &packageTypes);

} // namespace artest::extensions

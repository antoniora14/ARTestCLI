#pragma once
#include "Metadata.h"

namespace artest::sdk
{
// Portable source declarations own identity. Overrides preserve Generate's IDs;
// adding a component only needs a new explicit, permanent symbolic anchor.
class IdentityNamespace final
{
  public:
    explicit IdentityNamespace(std::string identity, std::map<std::string, std::string> overrides = {})
        : m_identity(std::move(identity)), m_overrides(std::move(overrides))
    {
        detail::MetadataId(m_identity);
        for (const auto &[anchor, id] : m_overrides)
        {
            CheckAnchor(anchor);
            detail::MetadataId(id);
        }
    }
    [[nodiscard]] const std::string &Id() const noexcept { return m_identity; }
    [[nodiscard]] std::string Component(std::string_view anchor) const
    {
        CheckAnchor(anchor);
        const auto found = m_overrides.find(std::string{anchor});
        auto id = found == m_overrides.end() ? m_identity + "." + std::string{anchor} : found->second;
        detail::MetadataId(id);
        return id;
    }
    [[nodiscard]] CommandInfo Command(std::string_view anchor, std::string name, ComponentMetadata metadata) const
    {
        return {Component(anchor), std::move(name), {}, std::move(metadata)};
    }
    [[nodiscard]] DriverInfo Driver(std::string_view anchor, std::string name, std::string contract,
                                    DriverMode mode, ComponentMetadata metadata) const
    {
        return {Component(anchor), std::move(name), std::move(contract), mode, {}, std::move(metadata)};
    }
  private:
    static void CheckAnchor(std::string_view anchor)
    {
        static const std::regex pattern{"[a-z0-9]+([.-][a-z0-9]+)*"};
        if (anchor.empty() || anchor.size() > 64 || !std::regex_match(anchor.begin(), anchor.end(), pattern))
            throw std::invalid_argument("Component anchor must be permanent lowercase letters/digits with dot or hyphen separators (maximum 64).");
    }
    std::string m_identity;
    std::map<std::string, std::string> m_overrides;
};
} // namespace artest::sdk

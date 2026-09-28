#pragma once

#include "Command.h"
#include "InstrumentDriver.h"
#include "Schema.h"
#include <concepts>
#include <memory>
#include <vector>

namespace artest::sdk
{
struct ComponentMetadata
{
    std::optional<Schema> schema;
    std::string schemaId; // Optional override; generated IDs have a stable v1 suffix.
    std::vector<std::string> requiredContracts;
    std::vector<std::string> aliases;
    std::string description;
};
struct CommandInfo
{
    std::string id;
    std::string name;
    std::string version; // Empty means the extension version.
    ComponentMetadata metadata;
};
enum class DriverMode
{
    Hardware,
    Simulated
};
struct DriverInfo
{
    std::string id;
    std::string name;
    std::string contract;
    DriverMode mode = DriverMode::Hardware;
    std::string version;
    ComponentMetadata metadata;
};

namespace detail
{
struct Registration
{
    std::string id, name, version, contract;
    bool simulated = false;
    std::unique_ptr<Command> (*commandFactory)() = nullptr;
    std::unique_ptr<InstrumentDriver> (*driverFactory)() = nullptr;
    ComponentMetadata metadata;
};
struct DefinitionAccess;
inline void RequireText(std::string_view text, std::string_view field)
{
    if (text.empty() || text.find('\0') != std::string_view::npos)
        throw std::invalid_argument(std::string{field} +
                                    " must be non-empty and contain no null bytes.");
}
inline void ValidateDescription(std::string_view text)
{
    // Validate UTF-8 scalars, not bytes; reject overlong sequences and surrogates.
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        unsigned value = first;
        unsigned tail = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { value = first & 31; tail = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { value = first & 15; tail = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { value = first & 7; tail = 3; minimum = 0x10000; }
        else if (first == 0 || first >= 0x80) throw std::invalid_argument("Description contains invalid UTF-8 or NUL.");
        for (unsigned n = 0; n < tail; ++n)
        {
            if (i == text.size()) throw std::invalid_argument("Description contains incomplete UTF-8.");
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) throw std::invalid_argument("Description contains invalid UTF-8.");
            value = (value << 6) | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            throw std::invalid_argument("Description contains invalid Unicode.");
        if (++count > 512) throw std::invalid_argument("Description exceeds 512 Unicode code points.");
    }
}
} // namespace detail

class Extension final
{
  public:
    Extension(std::string id, std::string version, std::string name = {}, std::string publisher = {})
        : m_id(std::move(id)), m_version(std::move(version)),
          m_name(std::move(name)), m_publisher(std::move(publisher))
    {
        detail::RequireText(m_id, "Extension ID");
        detail::RequireText(m_version, "Extension version");
    }

    template <class T>
        requires std::derived_from<T, Command> && std::default_initializable<T>
    void AddCommand(CommandInfo info)
    {
        Add({std::move(info.id), std::move(info.name), std::move(info.version),
             "artest.contract.command.v1", false,
             +[]() -> std::unique_ptr<Command> { return std::make_unique<T>(); }, nullptr,
             std::move(info.metadata)});
    }

    template <class T>
        requires std::derived_from<T, InstrumentDriver> && std::default_initializable<T>
    void AddDriver(DriverInfo info)
    {
        if (info.mode != DriverMode::Hardware && info.mode != DriverMode::Simulated)
            throw std::invalid_argument("Unknown driver mode.");
        Add({std::move(info.id), std::move(info.name), std::move(info.version),
             std::move(info.contract), info.mode == DriverMode::Simulated, nullptr,
             +[]() -> std::unique_ptr<InstrumentDriver> { return std::make_unique<T>(); },
             std::move(info.metadata)});
    }

  private:
    void Add(detail::Registration entry)
    {
        detail::RequireText(entry.id, "Component ID");
        detail::RequireText(entry.name, "Component name");
        detail::RequireText(entry.contract, "Component contract");
        detail::ValidateDescription(entry.metadata.description);
        if (entry.version.empty())
            entry.version = m_version;
        detail::RequireText(entry.version, "Component version");
        for (const auto &existing : m_components)
            if (existing.id == entry.id)
                throw std::invalid_argument("Duplicate component ID: " + entry.id + " (" + existing.name + " / " + entry.name + ")");
        m_components.push_back(std::move(entry));
    }
    friend struct detail::DefinitionAccess;
    std::string m_id, m_version, m_name, m_publisher;
    std::vector<detail::Registration> m_components;
};

namespace detail
{
// Internal read-only projection. Definitions become immutable before ABI use.
struct DefinitionAccess
{
    static const auto &Name(const Extension &value) noexcept { return value.m_name; }
    static const auto &Publisher(const Extension &value) noexcept { return value.m_publisher; }
    static const auto &Id(const Extension &value) noexcept
    {
        return value.m_id;
    }
    static const auto &Version(const Extension &value) noexcept
    {
        return value.m_version;
    }
    static const auto &Components(const Extension &value) noexcept
    {
        return value.m_components;
    }
};
} // namespace detail
} // namespace artest::sdk

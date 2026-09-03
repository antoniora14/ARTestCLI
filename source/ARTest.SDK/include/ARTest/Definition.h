#pragma once

#include "Command.h"
#include "InstrumentDriver.h"
#include <concepts>
#include <memory>
#include <vector>

namespace artest::sdk
{
struct CommandInfo
{
    std::string id;
    std::string name;
    std::string version; // Empty means the extension version.
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
};

namespace detail
{
struct Registration
{
    std::string id, name, version, contract;
    bool simulated = false;
    std::unique_ptr<Command> (*commandFactory)() = nullptr;
    std::unique_ptr<InstrumentDriver> (*driverFactory)() = nullptr;
};
struct DefinitionAccess;
inline void RequireText(std::string_view text, std::string_view field)
{
    if (text.empty() || text.find('\0') != std::string_view::npos)
        throw std::invalid_argument(std::string{field} +
                                    " must be non-empty and contain no null bytes.");
}
} // namespace detail

class Extension final
{
  public:
    Extension(std::string id, std::string version)
        : m_id(std::move(id)), m_version(std::move(version))
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
             +[]() -> std::unique_ptr<Command> { return std::make_unique<T>(); }, nullptr});
    }

    template <class T>
        requires std::derived_from<T, InstrumentDriver> && std::default_initializable<T>
    void AddDriver(DriverInfo info)
    {
        if (info.mode != DriverMode::Hardware && info.mode != DriverMode::Simulated)
            throw std::invalid_argument("Unknown driver mode.");
        Add({std::move(info.id), std::move(info.name), std::move(info.version),
             std::move(info.contract), info.mode == DriverMode::Simulated, nullptr,
             +[]() -> std::unique_ptr<InstrumentDriver> { return std::make_unique<T>(); }});
    }

  private:
    void Add(detail::Registration entry)
    {
        detail::RequireText(entry.id, "Component ID");
        detail::RequireText(entry.name, "Component name");
        detail::RequireText(entry.contract, "Component contract");
        if (entry.version.empty())
            entry.version = m_version;
        detail::RequireText(entry.version, "Component version");
        for (const auto &existing : m_components)
            if (existing.id == entry.id)
                throw std::invalid_argument("Duplicate component ID: " + entry.id);
        m_components.push_back(std::move(entry));
    }
    friend struct detail::DefinitionAccess;
    std::string m_id, m_version;
    std::vector<detail::Registration> m_components;
};

namespace detail
{
// Internal read-only projection. Definitions become immutable before ABI use.
struct DefinitionAccess
{
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

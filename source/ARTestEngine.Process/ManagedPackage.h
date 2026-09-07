#pragma once
#include "../ThirdParty/json.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace artest::process
{
enum class ManagedRuntimeKind { Python, DotNet };
struct ManagedRuntimeSpec
{
    ManagedRuntimeKind kind;
    std::string entry, entryPoint, runtimeVersion, dependencyLock;
    std::uint32_t protocolMajor = 0, protocolMinor = 1;
};
struct InventoryEntry { std::string path, sha256; };
struct ManagedPackageRequirements
{
    ManagedRuntimeSpec runtime;
    std::vector<InventoryEntry> files;
};
// Draft v3's managed-specific projection only. This performs no filesystem I/O,
// imports, execution, dependency restore or common component-schema validation.
ManagedPackageRequirements ParseManagedPackageRequirements(const nlohmann::json &manifest);
} // namespace artest::process

#include "ManagedPackage.h"
#include "Protocol.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace artest::process
{
namespace
{
void Require(bool valid, const char *message)
{
    if (!valid) throw ProcessError("MANAGED_MANIFEST_INVALID", message);
}
std::string Text(const nlohmann::json &object, const char *key)
{
    const auto found = object.find(key);
    Require(found != object.end() && found->is_string(), "Missing or non-string runtime property.");
    auto text = found->get<std::string>();
    Require(!text.empty() && text.size() <= 512, "Empty or oversized runtime property.");
    return text;
}
std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}
std::string RelativePath(std::string path)
{
    Require(!path.empty() && path.size() <= 512 && path.front() != '/' &&
            path.find_first_of("\\:") == std::string::npos, "Package paths must be portable relative paths.");
    Require(std::all_of(path.begin(), path.end(), [](unsigned char c) { return c >= 32 && c <= 126; }),
            "Draft managed paths use printable ASCII.");
    std::size_t start = 0;
    while (start <= path.size())
    {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        Require(!part.empty() && part != "." && part != ".." && part.back() != ' ' && part.back() != '.' &&
                part.find_first_of("<>\"|?*") == std::string::npos, "Unsafe package path segment.");
        const auto device = Lower(part.substr(0, part.find('.')));
        Require(device != "con" && device != "prn" && device != "aux" && device != "nul" &&
                !(device.size() == 4 && (device.substr(0, 3) == "com" || device.substr(0, 3) == "lpt") &&
                  device[3] >= '1' && device[3] <= '9'), "Windows device paths are not package files.");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return path;
}
}
ManagedPackageRequirements ParseManagedPackageRequirements(const nlohmann::json &manifest)
{
    Require(manifest.is_object() && manifest.contains("schemaVersion") && manifest["schemaVersion"] == 3 &&
            manifest.contains("runtime") && manifest["runtime"].is_object(), "A draft v3 runtime is required.");
    const auto &runtime = manifest["runtime"];
    const std::set<std::string> names{"kind", "entry", "entryPoint", "runtimeVersion",
        "dependencyLock", "protocol", "architecture", "isolation"};
    Require(runtime.size() == names.size(), "Missing or unknown runtime properties.");
    for (const auto &[key, value] : runtime.items())
    {
        (void)value;
        Require(names.contains(key), "Unknown runtime property.");
    }
    const auto kind = Text(runtime, "kind");
    Require(kind == "python" || kind == "dotnet", "Not a managed runtime.");
    Require(Text(runtime, "architecture") == "x64" && Text(runtime, "isolation") == "outOfProcess",
            "Only isolated x64 workers are described by this draft.");
    const auto &version = runtime.at("protocol");
    Require(version.is_object() && version.size() == 2 && version.contains("major") &&
            version.contains("minor") && version["major"].is_number_integer() &&
            version["minor"].is_number_integer() && version["major"] == ProtocolMajor &&
            version["minor"] == ProtocolMinor, "Unsupported process protocol.");
    ManagedPackageRequirements result;
    auto &spec = result.runtime;
    spec.kind = kind == "python" ? ManagedRuntimeKind::Python : ManagedRuntimeKind::DotNet;
    spec.entry = RelativePath(Text(runtime, "entry"));
    spec.dependencyLock = RelativePath(Text(runtime, "dependencyLock"));
    spec.entryPoint = Text(runtime, "entryPoint");
    Require(std::all_of(spec.entryPoint.begin(), spec.entryPoint.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == ':';
    }), "Entry point must be an explicit module/type identifier, never a command line.");
    spec.runtimeVersion = Text(runtime, "runtimeVersion");
    Require(spec.runtimeVersion == (kind == "python" ? "3.13" : "10.0"), "Runtime outside the initial target matrix.");
    Require(manifest.contains("inventory") && manifest["inventory"].is_array() &&
            !manifest["inventory"].empty() && manifest["inventory"].size() <= 4096,
            "Managed packages require a bounded full file inventory.");
    std::set<std::string> paths;
    bool entryPresent = false, lockPresent = false;
    for (const auto &item : manifest["inventory"])
    {
        Require(item.is_object() && item.size() == 2, "Invalid file inventory record.");
        InventoryEntry file{RelativePath(Text(item, "path")), Lower(Text(item, "sha256"))};
        Require(file.sha256.size() == 64 && std::all_of(file.sha256.begin(), file.sha256.end(),
                [](unsigned char c) { return std::isxdigit(c) != 0; }), "Invalid SHA-256 value.");
        Require(paths.insert(Lower(file.path)).second, "Duplicate case-insensitive package file.");
        lockPresent |= file.path == spec.dependencyLock;
        entryPresent |= kind == "python" ? file.path.starts_with(spec.entry + "/") : file.path == spec.entry;
        result.files.push_back(std::move(file));
    }
    Require(entryPresent && lockPresent, "Runtime entry and dependency lock must be inventoried.");
    return result;
}
} // namespace artest::process

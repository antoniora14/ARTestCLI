#include "ManagedIntegrity.h"
#include "FileIntegrity.h"
#include "CatalogValidation.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <set>
#include <thread>
#include <vector>
namespace artest::extensions
{
namespace
{
void Require(bool valid, const char *message)
{
    if (!valid) throw std::runtime_error(message);
}
struct InventoryFile
{
    std::filesystem::path path;
    std::string expectedHash;
};
void CheckHashes(const std::vector<InventoryFile> &files)
{
    std::vector<std::string> hashes(files.size());
    std::vector<std::exception_ptr> failures(files.size());
    const auto hashOne = [&files, &hashes, &failures](std::size_t index) {
        try { hashes[index] = Sha256(files[index].path); }
        catch (...) { failures[index] = std::current_exception(); }
    };

    constexpr std::size_t parallelThreshold = 64U;
    constexpr unsigned int maximumWorkers = 8U;
    if (files.size() < parallelThreshold)
    {
        for (std::size_t index = 0; index < files.size(); ++index) hashOne(index);
    }
    else
    {
        const auto available = (std::max)(1U, std::thread::hardware_concurrency());
        const auto workerCount = (std::min)(
            files.size(), static_cast<std::size_t>((std::min)(available, maximumWorkers)));
        std::atomic<std::size_t> next{0U};
        const auto worker = [&] {
            for (;;)
            {
                const auto index = next.fetch_add(1U, std::memory_order_relaxed);
                if (index >= files.size()) return;
                hashOne(index);
            }
        };
        std::vector<std::jthread> workers;
        workers.reserve(workerCount);
        for (std::size_t index = 0; index < workerCount; ++index) workers.emplace_back(worker);
    }

    // Retain deterministic diagnostics even though independent files are hashed concurrently.
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        if (failures[index]) std::rethrow_exception(failures[index]);
        Require(hashes[index] == files[index].expectedHash, "File inventory hash mismatch.");
    }
}
void CheckTree(const std::filesystem::path &root, const nlohmann::json &inventory,
               const std::string &excluded)
{
    Require(inventory.is_array() && inventory.size() <= 20000, "Invalid file inventory.");
    std::set<std::filesystem::path> expected;
    std::vector<InventoryFile> files;
    files.reserve(inventory.size());
    for (const auto &item : inventory)
    {
        const auto relative = std::filesystem::path{item.at("path").get<std::string>()};
        const auto path = std::filesystem::weakly_canonical(root / relative);
        Require(!relative.is_absolute() && IsContained(root, path) && std::filesystem::is_regular_file(path),
                "Inventory path escaped its root or is missing.");
        Require(expected.insert(path).second, "Duplicate inventory path.");
        files.push_back({path, item.at("sha256").get<std::string>()});
    }
    CheckHashes(files);
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        Require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "Reparse points are not allowed in managed packages/environments.");
        if (!entry.is_regular_file()) continue;
        if (entry.path() == root / excluded) continue;
        Require(expected.contains(entry.path()), "Uninventoried package/environment file.");
    }
}
}
process::ManagedPackageRequirements ValidateManagedPackage(const CatalogPackage &package)
{
    auto requirements = process::ParseManagedPackageRequirements(package.manifest);
    Require(requirements.runtime.kind == process::ManagedRuntimeKind::Python, ".NET activation is not implemented.");
    Require(std::filesystem::is_directory(package.entryPath), "Python entry must be a code directory.");
    CheckTree(package.packageRoot, package.manifest.at("inventory"), "artest-extension.json");
    return requirements;
}
PythonEnvironment ValidatePythonEnvironment(const CatalogPackage &package, const std::filesystem::path &receiptPath)
{
    Require(receiptPath.is_absolute() && std::filesystem::is_regular_file(receiptPath),
            "An absolute prepared Python environment receipt is required.");
    Require(std::filesystem::file_size(receiptPath) <= 8 * 1024 * 1024, "Oversized Python environment receipt.");
    std::ifstream input(receiptPath);
    const auto receipt = nlohmann::json::parse(input);
    Require(receipt.at("schemaVersion") == 1 && receipt.at("extensionId") == package.extensionId &&
            receipt.at("packageSha256") == Sha256(package.manifestPath), "Python environment/package mismatch.");
    const auto root = std::filesystem::weakly_canonical(receiptPath.parent_path());
    CheckTree(root, receipt.at("files"), "artest-environment.json");
    for (const auto &file : receipt.at("runtimeFiles"))
    {
        const std::filesystem::path path{file.at("path").get<std::string>()};
        Require(path.is_absolute() && Sha256(path) == file.at("sha256").get<std::string>(),
                "Pinned Python runtime library changed.");
    }
    PythonEnvironment result{receipt.at("interpreter").get<std::string>(), root / receipt.at("launcher").get<std::string>()};
    Require(result.interpreter.is_absolute() && std::filesystem::is_regular_file(result.interpreter) &&
            Sha256(result.interpreter) == receipt.at("interpreterSha256").get<std::string>(), "Pinned Python interpreter changed.");
    result.launcher = std::filesystem::weakly_canonical(result.launcher);
    Require(IsContained(root, result.launcher) && std::filesystem::is_regular_file(result.launcher),
            "Invalid Python launcher.");
    return result;
}
}

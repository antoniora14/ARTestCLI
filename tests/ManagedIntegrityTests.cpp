#include "ARTestEngine/Extensions/ManagedIntegrity.h"
#include "ARTestEngine/Extensions/FileIntegrity.h"
#include "ARTestEngine/Extensions/PrivatePath.h"
#include <gtest/gtest.h>
#include <Windows.h>
#include <winioctl.h>
#include <cstring>
#include <fstream>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
fs::path Access(const fs::path &path) { return fs::path{L"\\\\?\\" + path.wstring()}; }
void Write(const fs::path &path, const std::string &bytes)
{
    fs::create_directories(Access(path.parent_path()));
    std::ofstream output{Access(path), std::ios::binary};
    output << bytes;
    if (!output) throw std::runtime_error("Fixture write failed.");
}
bool Junction(const fs::path &link, const fs::path &target)
{
    fs::create_directory(Access(link));
    const auto substitute = L"\\??\\" + target.wstring(), print = target.wstring();
    const auto substituteSize = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    const auto printSize = static_cast<WORD>(print.size() * sizeof(wchar_t));
    const WORD dataSize = static_cast<WORD>(8 + substituteSize + 2 + printSize + 2);
    const WORD printOffset = static_cast<WORD>(substituteSize + 2);
    std::vector<BYTE> data(8U + dataSize, 0);
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(data.data(), &tag, 4); std::memcpy(data.data() + 4, &dataSize, 2);
    std::memcpy(data.data() + 10, &substituteSize, 2);
    std::memcpy(data.data() + 12, &printOffset, 2); std::memcpy(data.data() + 14, &printSize, 2);
    std::memcpy(data.data() + 16, substitute.data(), substituteSize);
    std::memcpy(data.data() + 16 + printOffset, print.data(), printSize);
    const HANDLE handle = CreateFileW(Access(link).c_str(), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0;
    const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, data.data(),
        static_cast<DWORD>(data.size()), nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(handle); return ok;
}
class ManagedIntegrityTests : public ::testing::TestWithParam<bool>
{
protected:
    fs::path owned, root, receiptPath, file;
    artest::extensions::CatalogPackage package;
    Json receipt;
    void SetUp() override
    {
        wchar_t temp[MAX_PATH]{}, name[MAX_PATH]{};
        ASSERT_NE(GetTempPathW(MAX_PATH, temp), 0U);
        ASSERT_NE(GetTempFileNameW(temp, L"mi6", 0, name), 0U);
        owned = name; ASSERT_TRUE(DeleteFileW(name)); fs::create_directory(owned);
        root = owned / "environment";
        if (GetParam()) while (root.native().size() < 205U) root /= "real-prepared-environment-path";
        receiptPath = root / "artest-environment.json";
        file = root / (GetParam() ? std::string(100, 'x') + ".pyc" : "module.pyc");
        Write(file, "original");
        Write(root / "launcher.py", "launcher"); Write(owned / "interpreter.exe", "interpreter");
        Write(owned / "manifest.json", "manifest");
        package.extensionId = "com.artest.test.integrity"; package.manifestPath = owned / "manifest.json";
        receipt = {{"schemaVersion", 1}, {"extensionId", package.extensionId},
            {"packageSha256", artest::extensions::Sha256(package.manifestPath)},
            {"files", Json::array()}, {"runtimeFiles", Json::array()},
            {"interpreter", (owned / "interpreter.exe").string()},
            {"interpreterSha256", artest::extensions::Sha256(owned / "interpreter.exe")}, {"launcher", "launcher.py"}};
        Add(file); Add(root / "launcher.py");
        // Above the parallel hashing threshold, on both short and long physical paths.
        for (int i = 0; i < 70; ++i) { const auto p = root / ("file-" + std::to_string(i)); Write(p, "payload"); Add(p); }
        Save();
    }
    void TearDown() override { if (!owned.empty()) fs::remove_all(Access(owned)); }
    void Add(const fs::path &path)
    {
        receipt["files"].push_back({{"path", path.lexically_relative(root).generic_string()},
            {"sha256", artest::extensions::Sha256(Access(path))}});
    }
    void Save() { Write(receiptPath, receipt.dump()); }
    void Validate() { (void)artest::extensions::ValidatePythonEnvironment(package, receiptPath); }
    void Rejects(const char *diagnostic)
    {
        try { Validate(); FAIL() << "Invalid fixture was accepted."; }
        catch (const std::runtime_error &error) { EXPECT_NE(std::string(error.what()).find(diagnostic), std::string::npos) << error.what(); }
    }
};
TEST_P(ManagedIntegrityTests, FullInventoryAndHashing)
{
    if (GetParam()) ASSERT_GT(file.native().size(), 300U);
    ASSERT_NO_THROW(Validate());
    EXPECT_NO_THROW((void)artest::extensions::ValidatePythonEnvironment(package, Access(receiptPath)));
}
TEST_P(ManagedIntegrityTests, MissingFile) { fs::remove(Access(file)); Rejects("Inventory path escaped its root or is missing."); }
TEST_P(ManagedIntegrityTests, AlteredFile) { Write(file, "altered"); Rejects("File inventory hash mismatch."); }
TEST_P(ManagedIntegrityTests, AdditionalFile) { Write(root / (std::string(100, 'z') + ".pyc"), "extra"); Rejects("Uninventoried package/environment file."); }
TEST_P(ManagedIntegrityTests, Escape)
{
    Write(root.parent_path() / "outside", "outside");
    receipt["files"][0]["path"] = "../outside"; Save(); Rejects("Inventory path escaped its root or is missing.");
}
TEST_P(ManagedIntegrityTests, AbsoluteInventoryPath)
{
    receipt["files"][0]["path"] = file.string(); Save(); Rejects("Inventory path escaped its root or is missing.");
}
TEST_P(ManagedIntegrityTests, DuplicateNormalizedPath)
{
    auto duplicate = receipt["files"][0]; duplicate["path"] = "./" + duplicate["path"].get<std::string>();
    receipt["files"].push_back(duplicate); Save(); Rejects("Duplicate inventory path.");
}
TEST_P(ManagedIntegrityTests, ReparseDirectory)
{
    fs::create_directory(Access(owned / "outside")); ASSERT_TRUE(Junction(root / "junction", owned / "outside"));
    Rejects("Reparse points are not allowed in managed packages/environments.");
    // Remove the junction itself before the owned fixture tree.
    ASSERT_TRUE(RemoveDirectoryW(Access(root / "junction").c_str()));
}
TEST_P(ManagedIntegrityTests, ReceiptAndLauncherBeyondMaxPath)
{
    auto deep = owned / "another-environment";
    while (deep.native().size() < 310U) deep /= "real-environment-directory";
    for (const auto &entry : receipt["files"])
    {
        const fs::path relative{entry.at("path").get<std::string>()};
        const auto destination = deep / relative;
        fs::create_directories(Access(destination.parent_path()));
        fs::copy_file(Access(root / relative), Access(destination));
    }
    Write(deep / "artest-environment.json", receipt.dump());
    ASSERT_GT((deep / "artest-environment.json").native().size(), 300U);
    EXPECT_NO_THROW((void)artest::extensions::ValidatePythonEnvironment(package, deep / "artest-environment.json"));
}
TEST(PrivateIntegrityPaths, RejectsDeviceNamespacesAndStreams)
{
    for (const auto *path : {L"\\\\.\\pipe\\artest", L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\file",
        L"\\??\\C:\\file", L"\\\\?\\Volume{123}\\file", L"C:relative", L"C:\\file:stream",
        L"C:\\NUL", L"C:\\CONOUT$", L"C:\\COM1.txt", L"C:\\LPT\u00b2", L"C:\\file. ", L"\\\\server",
        L"\\\\server\\pipe\\artest", L"\\\\server\\..\\file"})
        EXPECT_THROW((void)artest::extensions::private_path::Access(path), std::runtime_error) << fs::path(path);
}
TEST(PrivateIntegrityPaths, NormalizesOnlyOrdinaryDriveAndUncPaths)
{
    using artest::extensions::private_path::Access;
    EXPECT_EQ(Access(L"C:\\folder\\..\\file"), fs::path(L"\\\\?\\C:\\file"));
    EXPECT_EQ(Access(L"\\\\?\\C:\\file"), Access(L"C:\\file"));
    EXPECT_EQ(Access(L"\\\\server\\share\\folder\\..\\file"), fs::path(L"\\\\?\\UNC\\server\\share\\file"));
    EXPECT_EQ(Access(L"\\\\?\\UNC\\server\\share\\file"), Access(L"\\\\server\\share\\file"));
}
INSTANTIATE_TEST_SUITE_P(PhysicalPaths, ManagedIntegrityTests, ::testing::Values(false, true));
}

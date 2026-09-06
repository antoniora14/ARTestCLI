#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace artest::tests
{
// Regression catalogs must not load or count user extensions deployed beside
// the references. Own a temporary copy of exactly the four published packages.
class ReferenceCatalog final
{
  public:
    explicit ReferenceCatalog(const std::filesystem::path &published)
    {
        static std::atomic_uint next{0};
        m_root = std::filesystem::temp_directory_path() /
            ("ARTest-ReferenceCatalog-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(GetTickCount64()) + "-" + std::to_string(next++));
        if (!std::filesystem::create_directory(m_root))
            throw std::runtime_error("Could not create an isolated reference catalog.");
        try
        {
            for (const auto name : {"ARTestCmdHardware", "ARTestCmdSample",
                                    "ARTestDrvSimCAN", "ARTestDrvSimPower"})
                std::filesystem::copy(published / name, m_root / name,
                                      std::filesystem::copy_options::recursive);
        }
        catch (...)
        {
            Remove();
            throw;
        }
    }
    ~ReferenceCatalog() { Remove(); }
    ReferenceCatalog(const ReferenceCatalog &) = delete;
    ReferenceCatalog &operator=(const ReferenceCatalog &) = delete;
    const std::filesystem::path &Root() const noexcept { return m_root; }

  private:
    void Remove() noexcept
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }
    std::filesystem::path m_root;
};
} // namespace artest::tests

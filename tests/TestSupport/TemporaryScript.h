#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

class TemporaryScript final
{
public:
    explicit TemporaryScript(const std::string& content)
    {
        static std::atomic<unsigned long long> sequence{0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_directory = std::filesystem::temp_directory_path()
            / ("ARTestCLI.UnitTests." + std::to_string(timestamp) + "." + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_directory);
        m_path = m_directory / "script.json";
        std::ofstream output(m_path, std::ios::binary);
        if (!output)
        {
            throw std::runtime_error("Unable to create a temporary test script.");
        }
        output << content;
    }

    ~TemporaryScript()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    TemporaryScript(const TemporaryScript&) = delete;
    TemporaryScript& operator=(const TemporaryScript&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_directory;
    std::filesystem::path m_path;
};

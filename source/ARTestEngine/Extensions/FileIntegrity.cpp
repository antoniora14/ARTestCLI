#include "FileIntegrity.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <array>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
namespace artest::extensions
{
[[nodiscard]] std::string Sha256(const std::filesystem::path &path)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0U;
    DWORD bytesWritten = 0U;
    std::vector<unsigned char> object;
    std::array<unsigned char, 32U> digest{};

    struct HashGuard
    {
        BCRYPT_ALG_HANDLE &algorithm;
        BCRYPT_HASH_HANDLE &hash;
        ~HashGuard()
        {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0U);
        }
    } guard{algorithm, hash};

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0U) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                          sizeof(objectSize), &bytesWritten, 0U) < 0)
    {
        throw std::runtime_error("Windows could not initialize SHA-256.");
    }
    object.resize(objectSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0U, 0U) < 0)
    {
        throw std::runtime_error("Windows could not create a SHA-256 hash.");
    }

    std::ifstream input{path, std::ios::binary};
    if (!input)
    {
        throw std::runtime_error("The extension entry could not be opened for hashing.");
    }
    std::array<char, 64U * 1024U> buffer{};
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                                        static_cast<ULONG>(count), 0U) < 0)
        {
            throw std::runtime_error("Windows failed while hashing the extension entry.");
        }
    }
    if (input.bad() ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0U) < 0)
    {
        throw std::runtime_error("The extension entry SHA-256 could not be completed.");
    }

    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (const auto byte : digest)
        text << std::setw(2) << static_cast<unsigned int>(byte);
    return text.str();
}

} // namespace artest::extensions

#pragma once
#include <filesystem>
#include <string>
namespace artest::extensions
{
std::string Sha256(const std::filesystem::path &path);

}

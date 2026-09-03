#pragma once
#include "../../ARTestEngine.Core/Catalog/ComponentDescriptor.h"
#include <cstdint>
#include <string>
#include <vector>

namespace artest::extensions
{
struct RuntimeDescriptor
{
    std::string kind;
    std::string entry;
    std::string isolation;
    std::string architecture;
    std::uint32_t abiMajor = 0;
    std::uint32_t abiMinor = 0;
};
struct IntegrityDescriptor
{
    std::string declaredSha256;
    std::string contentSha256;
    bool signatureDeclared = false;
};
struct ExtensionDescriptor
{
    std::string extensionId;
    std::string version;
    std::string displayName;
    std::string publisher;
    RuntimeDescriptor runtime;
    IntegrityDescriptor integrity;
    std::vector<ComponentDescriptor> components;
};
} // namespace artest::extensions

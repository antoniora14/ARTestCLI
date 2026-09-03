#pragma once

#include "../../ThirdParty/json.hpp"
#include <string>
#include <vector>

namespace artest
{
enum class ComponentKind
{
    Command,
    InstrumentDriver,
    Tool
};

struct SchemaBinding
{
    std::string role;
    std::string schemaId;
    std::string mediaType;
    std::string relativePath;
    nlohmann::json document;
};

// Metadata is a value, never a factory or executable extension object.
struct ComponentDescriptor
{
    ComponentKind kind = ComponentKind::Command;
    std::string typeId;
    std::string contractId;
    std::string version;
    std::string displayName;
    std::vector<std::string> aliases;
    std::vector<std::string> capabilities;
    std::vector<std::string> requiredContracts;
    std::vector<SchemaBinding> schemas;
    std::vector<std::string> flags;
    std::string validationCode;
    std::string unavailableCode;

    [[nodiscard]] const SchemaBinding *Schema(const std::string &role) const noexcept
    {
        for (const auto &binding : schemas)
            if (binding.role == role)
                return &binding;
        return nullptr;
    }
};
} // namespace artest

#pragma once

#include "../Diagnostics.h"
#include "ComponentDescriptor.h"
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace artest
{
// A catalog value is an immutable compilation snapshot after construction.
// EngineContext serializes replacement; compiled plans retain their revision.
class ComponentCatalog final
{
  public:
    [[nodiscard]] OperationResult Add(const std::vector<ComponentDescriptor> &descriptors)
    {
        auto next = m_components;
        for (const auto &descriptor : descriptors)
        {
            auto names = descriptor.aliases;
            names.push_back(descriptor.typeId);
            for (const auto &name : names)
            {
                if (name.empty() || !next.emplace(name, descriptor).second)
                    return OperationResult::Failure(
                        "EXTENSION_COMPONENT_DUPLICATE",
                        "Component identities and aliases must be globally unique.", name);
            }
        }
        m_components.swap(next);
        return OperationResult::Success();
    }

    [[nodiscard]] const ComponentDescriptor *Find(const std::string &name) const noexcept
    {
        const auto found = m_components.find(name);
        return found == m_components.end() ? nullptr : &found->second;
    }

  private:
    std::map<std::string, ComponentDescriptor> m_components;
};
} // namespace artest

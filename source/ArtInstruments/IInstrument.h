#pragma once
#include <string>
#include "../ThirdParty/json.hpp"
#include "../ArtCore/Diagnostics.h"

class IInstrument
{
public:
    virtual ~IInstrument() = default;

    virtual std::string GetId() const = 0;
    virtual void SetId(std::string id) = 0;
    [[nodiscard]] virtual OperationResult Initialize(const nlohmann::json& params) = 0;
    virtual void Shutdown() noexcept = 0;
};

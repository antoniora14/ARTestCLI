#pragma once

#include "../Diagnostics.h"
#include "../../ThirdParty/json.hpp"

#include <string>

namespace artest
{
    class IInstrument
    {
    public:
        virtual ~IInstrument() = default;

        [[nodiscard]] virtual std::string GetId() const = 0;
        virtual void SetId(std::string id) = 0;
        [[nodiscard]] virtual OperationResult Initialize(const nlohmann::json& configuration) = 0;
        [[nodiscard]] virtual OperationResult Shutdown() = 0;
    };
}

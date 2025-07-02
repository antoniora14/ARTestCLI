#pragma once
#include <string>
#include "../ThirdParty/json.hpp"

class IInstrument
{
public:
    virtual ~IInstrument() = default;

    virtual std::string GetId() const = 0;
    virtual void Initialize(const nlohmann::json& params) = 0;
    
};

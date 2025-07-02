#pragma once
#include <string>
#include "../ArtInstruments/IInstrument.h"
#include "../ThirdParty/json.hpp"
#include "ExecutionContext.h"

class IInstrument;

class ICommand 
{
public:
    virtual ~ICommand() = default;

    virtual std::string Name() const = 0;
    virtual void Execute(ExecutionContext& context) = 0;
    virtual void Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument) = 0;
    virtual bool Validate(std::string& csError) = 0;
};
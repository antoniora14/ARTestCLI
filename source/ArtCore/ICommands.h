#pragma once
#include <string>
#include "../ArtInstruments/IInstrument.h"
#include "../ThirdParty/json.hpp"
#include "ExecutionContext.h"
#include "ExecutionResult.h"
#include <cstdint>
#include <memory>

class IInstrument;

class ICommand 
{
public:
    virtual ~ICommand() = default;

    virtual std::string Name() const = 0;
    virtual StepResult Execute(ExecutionContext& context) = 0;
    virtual void Configure(const nlohmann::json& params, std::shared_ptr<IInstrument> spInstrument) = 0;
    virtual bool Validate(std::string& csError) const = 0;
};

struct CommandInstance
{
    std::uint64_t stepId = 0;
    std::unique_ptr<ICommand> command;
};

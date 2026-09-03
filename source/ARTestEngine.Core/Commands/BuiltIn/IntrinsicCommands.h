#pragma once
#include "../../Catalog/ComponentCatalog.h"
#include "../CommandRegistry.h"
#include "WaitCommand.h"
#include "IfCommand.h"

namespace artest
{
    inline OperationResult RegisterIntrinsicMetadata(ComponentCatalog& catalog)
    {
        ComponentDescriptor wait;
        wait.typeId = WaitCommandName;
        wait.contractId = "artest.contract.command.v1";
        wait.version = "1.0.0";
        wait.displayName = "Wait";
        wait.validationCode = "WAIT_DURATION_INVALID";
        wait.schemas.push_back({"parameters", "artest.schema.wait.parameters.v1",
            "application/json; charset=utf-8", {}, {
                {"type", "object"}, {"required", {"milliseconds"}},
                {"properties", {{"milliseconds", {{"type", "integer"}, {"minimum", 0}, {"maximum", 2147483647}}}}},
                {"additionalProperties", false}}});
        ComponentDescriptor branch;
        branch.typeId = IfCommandName;
        branch.contractId = "artest.contract.command.v1";
        branch.version = "1.0.0";
        branch.unavailableCode = "IF_NOT_IMPLEMENTED";
        return catalog.Add({wait, branch});
    }

    inline OperationResult RegisterIntrinsicCommands(CommandRegistry& registry)
    {
        auto result = registry.Register(WaitCommandName, [] { return std::make_unique<WaitCommand>(); });
        if (!result.Succeeded()) return result;
        return registry.Register(IfCommandName, [] { return std::make_unique<IfCommand>(); });
    }
}

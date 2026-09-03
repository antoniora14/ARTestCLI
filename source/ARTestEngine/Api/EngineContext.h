#pragma once
#include "../../ARTestEngine.Core/Catalog/ComponentCatalog.h"
#include "../Extensions/ExtensionCatalog.h"
#include "../Extensions/NativeExtensionRuntime.h"
#include "EngineEvents.h"
namespace artest::engine
{
// The preparation snapshot is data-only; activation is the executable boundary.
struct EngineContext
{
    EngineContext();
    artest::OperationResult Initialize(bool discoverDefault);
    artest::OperationResult Prepare(const std::filesystem::path &root);
    artest::OperationResult Activate();
    EventHub events;
    std::shared_ptr<artest::extensions::NativeExtensionRuntime> runtime;
    artest::CommandRegistry commands;
    artest::InstrumentRegistry instruments;
    artest::ComponentCatalog catalog;
    artest::extensions::CatalogScan prepared;
    nlohmann::json lastCatalogReport;
    std::uint64_t revision = 0;
    bool active = false;
    bool loading = false;
    std::mutex mutex;
    std::weak_ptr<int> runLease;
};

} // namespace artest::engine

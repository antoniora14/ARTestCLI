#include "NativeComponentAdapters.h"
#include "NativeModuleLoader.h"
#include "NativeRuntimeState.h"
namespace artest::extensions
{
NativeExtensionRuntime::NativeExtensionRuntime(IEventSink &eventSink)
    : m_implementation(std::make_unique<Implementation>(eventSink))
{
}
NativeExtensionRuntime::~NativeExtensionRuntime() = default;

nlohmann::json NativeExtensionRuntime::ValidateCatalog(
    const std::filesystem::path &approvedRoot) const
{
    const auto scan = m_implementation->catalog.Discover(approvedRoot);
    std::scoped_lock lock{m_implementation->catalogMutex};
    return scan.ToJson(scan.IsValid() ? "validated" : "rejected",
                       m_implementation->catalogGeneration, nlohmann::json::array());
}

OperationResult NativeExtensionRuntime::Refresh(const std::filesystem::path &approvedRoot,
                                                CommandRegistry &commands,
                                                InstrumentRegistry &instruments,
                                                const std::string &expectedFingerprint)
{
    {
        std::scoped_lock lock{m_implementation->catalogMutex};
        if (m_implementation->activating || !m_implementation->modules.empty())
            return OperationResult::Failure(
                "EXTENSION_CATALOG_ALREADY_LOADED",
                "Catalog activation is in progress or the catalog is already active.");
        m_implementation->activating = true;
    }
    struct ActivationGuard
    {
        Implementation &state;
        ~ActivationGuard()
        {
            std::scoped_lock lock{state.catalogMutex};
            state.activating = false;
        }
    } activation{*m_implementation};
    // Extension callbacks may reenter catalog inspection; never load under its mutex.

    auto scan = m_implementation->catalog.Discover(approvedRoot);
    const auto collectDiagnostics = [&scan] {
        OperationResult result{scan.diagnostics};
        for (const auto &package : scan.packages)
            result.diagnostics.insert(result.diagnostics.end(), package.diagnostics.begin(),
                                      package.diagnostics.end());
        return result;
    };
    const auto reject = [this, &scan, &collectDiagnostics] {
        auto result = collectDiagnostics();
        std::scoped_lock lock{m_implementation->catalogMutex};
        m_implementation->lastScan = std::move(scan);
        m_implementation->catalogStatus = "rejected";
        return result;
    };
    if (!scan.IsValid())
        return reject();
    if (!expectedFingerprint.empty() && scan.Fingerprint() != expectedFingerprint)
    {
        scan.diagnostics.push_back({DiagnosticSeverity::Error, "EXTENSION_CATALOG_CHANGED",
                                    "The package bytes or schemas changed after offline "
                                    "preparation. Prepare and compile again.",
                                    approvedRoot.string()});
        return reject();
    }

    try
    {
        auto candidate = LoadNativeModules(scan, m_implementation->broker.hostApi);
        auto &loaded = candidate.modules;
        auto &types = candidate.types;
        if (!scan.IsValid())
            return reject();

        const auto self = shared_from_this();
        std::vector<RegistryTransaction::Command> commandBatch;
        std::vector<RegistryTransaction::Instrument> instrumentBatch;
        for (const auto &[typeId, entry] : types)
        {
            if (entry.second.kind == ARTEST_COMPONENT_KIND_COMMAND)
                commandBatch.push_back(
                    {typeId, [self, typeId] { return MakeNativeCommand(self, typeId); }});
            else if (entry.second.kind == ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER)
                instrumentBatch.push_back({typeId, [self, typeId](IEventSink &) {
                                               return MakeNativeInstrument(self, typeId);
                                           }});
        }
        std::string activeStatus = "active";
        const EngineEvent activatedEvent{
            EngineEventKind::Diagnostic, EngineEventSeverity::Information, "extension-catalog",
            "Native extension catalog validated and activated atomically."};
        std::unique_lock publishLock{m_implementation->catalogMutex};
        auto committed =
            RegistryTransaction::Commit(commands, instruments, commandBatch, instrumentBatch);
        if (!committed.Succeeded())
        {
            publishLock.unlock();
            scan.diagnostics.insert(scan.diagnostics.end(), committed.diagnostics.begin(),
                                    committed.diagnostics.end());
            return reject();
        }
        m_implementation->registration = std::move(*committed.value);

        m_implementation->modules = std::move(loaded);
        m_implementation->types = std::move(types);
        m_implementation->lastScan = std::move(scan);
        m_implementation->catalogStatus.swap(activeStatus);
        ++m_implementation->catalogGeneration;
        publishLock.unlock();
        m_implementation->eventSink.Publish(activatedEvent);
        return OperationResult::Success();
    }
    catch (const std::exception &exception)
    {
        scan.diagnostics.push_back({DiagnosticSeverity::Error, "EXTENSION_CATALOG_EXCEPTION",
                                    exception.what(), approvedRoot.string()});
        return reject();
    }
}

nlohmann::json NativeExtensionRuntime::CatalogSnapshot() const
{
    std::scoped_lock lock{m_implementation->catalogMutex};
    nlohmann::json active = nlohmann::json::array();
    for (const auto &module : m_implementation->modules)
        active.push_back(module->manifest);
    return m_implementation->lastScan.ToJson(m_implementation->catalogStatus,
                                             m_implementation->catalogGeneration, active);
}

} // namespace artest::extensions

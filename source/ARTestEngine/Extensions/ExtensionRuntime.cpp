#include "ComponentAdapters.h"
#include "NativeModuleLoader.h"
#include "ExtensionRuntimeState.h"
namespace artest::extensions
{
ExtensionRuntime::ExtensionRuntime(IEventSink &eventSink)
    : m_implementation(std::make_unique<Implementation>(eventSink))
{
    m_implementation->broker.invoke = [this](const auto &component, auto operation, auto request,
        auto invocation, auto sink, auto error) {
        const auto callerMinor = m_implementation->callerAbiMinor;
        const auto status = InvokeAbi(component, operation, request, invocation, sink, error);
        // ABI 0.1 callers receive only old base statuses. The Engine's root
        // scope still retains uncertainty even if that caller wraps the error.
        return callerMinor < 2 ? status & ~ARTEST_STATUS_EFFECT_INDETERMINATE_FLAG : status;
    };
}
ExtensionRuntime::~ExtensionRuntime() = default;
void ExtensionRuntime::Configure(const nlohmann::json &options)
{
    m_implementation->python.Configure(options.value("pythonEnvironments", nlohmann::json::object()));
}
OperationResult ExtensionRuntime::BeginSession()
{
    return OperationResult::Success();
}
OperationResult ExtensionRuntime::EndSession() { return m_implementation->python.EndSession(); }

nlohmann::json ExtensionRuntime::ValidateCatalog(
    const std::filesystem::path &approvedRoot) const
{
    const auto scan = m_implementation->catalog.Discover(approvedRoot);
    std::scoped_lock lock{m_implementation->catalogMutex};
    return scan.ToJson(scan.IsValid() ? "validated" : "rejected",
                       m_implementation->catalogGeneration, nlohmann::json::array());
}

OperationResult ExtensionRuntime::Refresh(const std::filesystem::path &approvedRoot,
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
        m_implementation->python.Load(scan);

        const auto self = shared_from_this();
        std::vector<RegistryTransaction::Command> commandBatch;
        std::vector<RegistryTransaction::Instrument> instrumentBatch;
        for (const auto &[typeId, entry] : types)
        {
            if (entry.second.kind == ARTEST_COMPONENT_KIND_COMMAND)
                commandBatch.push_back(
                    {typeId, [self, typeId] { return MakeExtensionCommand(self, typeId); }});
            else if (entry.second.kind == ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER)
                instrumentBatch.push_back({typeId, [self, typeId](IEventSink &) {
                                               return MakeExtensionInstrument(self, typeId);
                                           }});
        }
        for (const auto &package : scan.packages)
            if (package.descriptor.runtime.kind == "python")
                for (const auto &component : package.descriptor.components)
                {
                    const auto typeId = component.typeId;
                    if (component.kind == ComponentKind::Command)
                        commandBatch.push_back({typeId, [self, typeId] { return MakeExtensionCommand(self, typeId); }});
                    else if (component.kind == ComponentKind::InstrumentDriver)
                        instrumentBatch.push_back({typeId, [self, typeId](IEventSink &) {
                            return MakeExtensionInstrument(self, typeId); }});
                }
        std::string activeStatus = "active";
        const EngineEvent activatedEvent{
            EngineEventKind::Diagnostic, EngineEventSeverity::Information, "extension-catalog",
            "Extension catalog validated and activated atomically."};
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

nlohmann::json ExtensionRuntime::CatalogSnapshot() const
{
    std::scoped_lock lock{m_implementation->catalogMutex};
    nlohmann::json active = nlohmann::json::array();
    for (const auto &module : m_implementation->modules)
        active.push_back(module->manifest);
    return m_implementation->lastScan.ToJson(m_implementation->catalogStatus,
                                             m_implementation->catalogGeneration, active);
}

} // namespace artest::extensions

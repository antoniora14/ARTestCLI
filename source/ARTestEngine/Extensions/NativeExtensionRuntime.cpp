#include "NativeExtensionRuntime.h"
#include "ExtensionCatalog.h"

#include "../../ARTest.SDK/include/ARTestExtensionAbi.h"
#include "../../ARTestEngine.Core/Commands/ICommand.h"
#include "../../ARTestEngine.Core/Execution/ExecutionContext.h"
#include "../../ARTestEngine.Core/Instruments/IInstrument.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] std::string ToString(ARTestStringView value)
    {
        return value.data == nullptr ? std::string{} : std::string{value.data, value.size};
    }

    [[nodiscard]] ARTestStringView View(const std::string& value) noexcept
    {
        return {value.data(), value.size()};
    }

    [[nodiscard]] ARTestPayloadView JsonPayload(const std::string& value) noexcept
    {
        static const std::string schema = "artest.schema.generic-json.v1";
        static const std::string media = "application/json; charset=utf-8";
        return {
            sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_JSON_UTF8,
            View(schema), View(media),
            {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}};
    }

    void SetError(ARTestErrorBuffer* error, const std::string& message) noexcept
    {
        if (error == nullptr) return;
        error->required_size = message.size() + 1U;
        if (error->data == nullptr || error->capacity == 0U) return;
        const auto count = (std::min)(message.size(), error->capacity - 1U);
        std::copy_n(message.data(), count, error->data);
        error->data[count] = '\0';
    }

    struct ErrorStorage
    {
        char text[1024]{};
        ARTestErrorBuffer buffer{sizeof(ARTestErrorBuffer), 0U, text, sizeof(text), 0U};
        [[nodiscard]] std::string Message(std::string fallback) const
        {
            return text[0] == '\0' ? std::move(fallback) : std::string{text};
        }
    };

    [[nodiscard]] ARTestComponentKind ParseKind(const std::string& value)
    {
        if (value == "command") return ARTEST_COMPONENT_KIND_COMMAND;
        if (value == "instrumentDriver") return ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER;
        if (value == "tool") return ARTEST_COMPONENT_KIND_TOOL;
        return 0U;
    }
}

namespace artest::extensions
{
    struct ComponentRecord
    {
        ARTestComponentKind kind = 0U;
        ARTestComponentFlags flags = ARTEST_COMPONENT_FLAG_NONE;
        std::string typeId;
        std::string contractId;
        std::string version;
        std::string displayName;
    };

    struct NativeModule
    {
        ~NativeModule()
        {
            if (extension != nullptr && api.destroy_extension != nullptr)
                api.destroy_extension(extension);
            if (library != nullptr) FreeLibrary(library);
        }
        std::filesystem::path packageRoot;
        nlohmann::json manifest;
        std::string manifestText;
        std::string extensionId;
        HMODULE library = nullptr;
        ARTestExtensionApiV0 api{};
        ARTestExtensionHandle extension = nullptr;
        std::vector<ComponentRecord> components;
        mutable std::recursive_mutex invocationMutex;
    };

    class NativeComponentInstance final
    {
    public:
        NativeComponentInstance(
            std::shared_ptr<NativeModule> owner,
            ComponentRecord descriptor,
            ARTestComponentHandle value) noexcept
            : module(std::move(owner)), record(std::move(descriptor)), handle(value) {}
        ~NativeComponentInstance()
        {
            if (handle != nullptr)
            {
                std::scoped_lock lock{module->invocationMutex};
                module->api.destroy_component(module->extension, handle);
            }
        }
        std::shared_ptr<NativeModule> module;
        ComponentRecord record;
        ARTestComponentHandle handle = nullptr;
    };

    class NativeInstrumentAdapter final : public IInstrument
    {
    public:
        NativeInstrumentAdapter(
            std::shared_ptr<NativeExtensionRuntime> runtime, std::string typeId) noexcept
            : m_runtime(std::move(runtime)), m_typeId(std::move(typeId)) {}
        [[nodiscard]] std::string GetId() const override { return m_id; }
        void SetId(std::string id) override { m_id = std::move(id); }
        [[nodiscard]] OperationResult Initialize(const nlohmann::json& configuration) override
        {
            auto created = m_runtime->CreateComponent(m_typeId, configuration);
            if (!created.Succeeded()) return {std::move(created.diagnostics)};
            m_component = std::move(*created.value);
            auto initialized = m_runtime->Invoke(
                m_component, "artest.lifecycle.initialize.v1",
                nlohmann::json::object(), nullptr, nullptr);
            if (!initialized.Succeeded())
            {
                m_component.reset();
                return initialized;
            }
            auto registered = m_runtime->RegisterService(m_id, m_component);
            if (!registered.Succeeded())
            {
                static_cast<void>(m_runtime->Invoke(
                    m_component, "artest.lifecycle.shutdown.v1",
                    nlohmann::json::object(), nullptr, nullptr));
                m_component.reset();
            }
            return registered;
        }
        [[nodiscard]] OperationResult Shutdown() override
        {
            m_runtime->UnregisterService(m_id);
            if (!m_component) return OperationResult::Success();
            auto result = m_runtime->Invoke(
                m_component, "artest.lifecycle.shutdown.v1",
                nlohmann::json::object(), nullptr, nullptr);
            m_component.reset();
            return result;
        }
    private:
        std::shared_ptr<NativeExtensionRuntime> m_runtime;
        std::string m_typeId;
        std::string m_id;
        std::shared_ptr<NativeComponentInstance> m_component;
    };

    class NativeCommandAdapter final : public ICommand
    {
    public:
        NativeCommandAdapter(
            std::shared_ptr<NativeExtensionRuntime> runtime, std::string typeId) noexcept
            : m_runtime(std::move(runtime)), m_typeId(std::move(typeId)) {}
        [[nodiscard]] std::string Name() const override { return m_typeId; }
        [[nodiscard]] OperationResult Configure(
            const nlohmann::json& parameters,
            std::shared_ptr<IInstrument> instrument) override
        {
            m_request = {
                {"parameters", parameters},
                {"instrumentId", instrument ? instrument->GetId() : std::string{}}};
            auto created = m_runtime->CreateComponent(m_typeId, parameters);
            if (!created.Succeeded()) return {std::move(created.diagnostics)};
            m_component = std::move(*created.value);
            return OperationResult::Success();
        }
        [[nodiscard]] OperationResult Validate() const override
        {
            if (!m_component)
                return OperationResult::Failure(
                    "EXTENSION_COMMAND_NOT_CONFIGURED", "The extension command is not configured.");
            return m_runtime->Invoke(
                m_component, "artest.component.validate.v1", m_request, nullptr, nullptr);
        }
        [[nodiscard]] StepResult Execute(
            ExecutionContext&, const CancellationToken& cancellation) override
        {
            nlohmann::json response;
            const auto result = m_runtime->Invoke(
                m_component, "artest.command.execute.v1",
                m_request, &cancellation, &response);
            if (result.Succeeded())
                return StepResult::Pass(
                    response.value("message", std::string{"Extension command passed."}));
            const auto message = result.diagnostics.empty()
                ? std::string{"Extension command failed."}
                : result.diagnostics.front().message;
            if (cancellation.IsTimedOut()) return StepResult::Timeout(message);
            if (cancellation.IsCancellationRequested()) return StepResult::Cancel(message);
            return StepResult::Error(message);
        }
    private:
        std::shared_ptr<NativeExtensionRuntime> m_runtime;
        std::string m_typeId;
        nlohmann::json m_request;
        std::shared_ptr<NativeComponentInstance> m_component;
    };

    class NativeExtensionRuntime::Implementation
    {
    public:
        explicit Implementation(IEventSink& sink) noexcept : eventSink(sink)
        {
            hostApi = {
                sizeof(ARTestHostApiV0), ARTEST_EXTENSION_ABI_MAJOR,
                ARTEST_EXTENSION_ABI_MINOR, 0U, this,
                &Log, &MonotonicTime, &ResolveService, &InvokeService, &ReleaseService};
        }
        struct ServiceLease { std::shared_ptr<NativeComponentInstance> component; };
        static void ARTEST_ABI_CALL Log(
            void* context, ARTestLogSeverity severity,
            ARTestStringView category, ARTestStringView message) noexcept
        {
            auto& self = *static_cast<Implementation*>(context);
            self.eventSink.Publish({
                EngineEventKind::Diagnostic,
                severity == ARTEST_LOG_ERROR ? EngineEventSeverity::Error
                    : severity == ARTEST_LOG_WARNING ? EngineEventSeverity::Warning
                    : EngineEventSeverity::Information,
                ToString(category), ToString(message)});
        }
        static std::uint64_t ARTEST_ABI_CALL MonotonicTime(void*) noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        static ARTestStatus ARTEST_ABI_CALL ResolveService(
            void* context, ARTestStringView contractId, ARTestStringView instanceId,
            ARTestServiceHandle* service, ARTestErrorBuffer* error) noexcept
        {
            if (service == nullptr)
            {
                SetError(error, "A service output pointer is required.");
                return ARTEST_STATUS_INVALID_ARGUMENT;
            }
            auto& self = *static_cast<Implementation*>(context);
            try
            {
                std::scoped_lock lock{self.serviceMutex};
                const auto found = self.services.find(ToString(instanceId));
                auto component = found == self.services.end() ? nullptr : found->second.lock();
                if (!component || component->record.contractId != ToString(contractId))
                {
                    SetError(error, "The configured service instance was not found.");
                    return ARTEST_STATUS_NOT_FOUND;
                }
                *service = reinterpret_cast<ARTestServiceHandle>(
                    new ServiceLease{std::move(component)});
                return ARTEST_STATUS_OK;
            }
            catch (...)
            {
                SetError(error, "The host failed while resolving a service.");
                return ARTEST_STATUS_HOST_FAILURE;
            }
        }
        static ARTestStatus ARTEST_ABI_CALL InvokeService(
            void*, ARTestServiceHandle service, ARTestStringView operation,
            const ARTestPayloadView* request, const ARTestInvocationContextV0* invocation,
            const ARTestResultSinkV0* resultSink, ARTestErrorBuffer* error) noexcept
        {
            if (service == nullptr) return ARTEST_STATUS_INVALID_ARGUMENT;
            auto& component = *reinterpret_cast<ServiceLease*>(service)->component;
            std::scoped_lock lock{component.module->invocationMutex};
            return component.module->api.invoke_component(
                component.module->extension, component.handle, operation,
                request, invocation, resultSink, error);
        }
        static void ARTEST_ABI_CALL ReleaseService(void*, ARTestServiceHandle service) noexcept
        {
            delete reinterpret_cast<ServiceLease*>(service);
        }
        IEventSink& eventSink;
        ARTestHostApiV0 hostApi{};
        std::vector<std::shared_ptr<NativeModule>> modules;
        std::unordered_map<std::string,
            std::pair<std::shared_ptr<NativeModule>, ComponentRecord>> types;
        std::map<std::string, std::weak_ptr<NativeComponentInstance>> services;
        mutable std::mutex serviceMutex;
        ExtensionCatalog catalog;
        CatalogScan lastScan;
        std::string catalogStatus = "notLoaded";
        std::uint64_t catalogGeneration = 0U;
        mutable std::mutex catalogMutex;
    };

    NativeExtensionRuntime::NativeExtensionRuntime(IEventSink& eventSink)
        : m_implementation(std::make_unique<Implementation>(eventSink)) {}
    NativeExtensionRuntime::~NativeExtensionRuntime() = default;

    nlohmann::json NativeExtensionRuntime::ValidateCatalog(
        const std::filesystem::path& approvedRoot) const
    {
        const auto scan = m_implementation->catalog.Discover(approvedRoot);
        std::scoped_lock lock{m_implementation->catalogMutex};
        return scan.ToJson(
            scan.IsValid() ? "validated" : "rejected",
            m_implementation->catalogGeneration,
            nlohmann::json::array());
    }

    OperationResult NativeExtensionRuntime::Refresh(
        const std::filesystem::path& approvedRoot,
        CommandRegistry& commands,
        InstrumentRegistry& instruments)
    {
        std::scoped_lock lock{m_implementation->catalogMutex};
        if (!m_implementation->modules.empty())
            return OperationResult::Failure(
                "EXTENSION_CATALOG_ALREADY_LOADED",
                "The active in-process catalog is immutable for the engine lifetime.");

        auto scan = m_implementation->catalog.Discover(approvedRoot);
        const auto collectDiagnostics = [&scan]
        {
            OperationResult result{scan.diagnostics};
            for (const auto& package : scan.packages)
                result.diagnostics.insert(result.diagnostics.end(),
                    package.diagnostics.begin(), package.diagnostics.end());
            return result;
        };
        const auto reject = [this, &scan, &collectDiagnostics]
        {
            auto result = collectDiagnostics();
            m_implementation->lastScan = std::move(scan);
            m_implementation->catalogStatus = "rejected";
            return result;
        };
        if (!scan.IsValid()) return reject();

        try
        {
            std::vector<std::shared_ptr<NativeModule>> loaded;
            std::unordered_map<std::string,
                std::pair<std::shared_ptr<NativeModule>, ComponentRecord>> types;
            const auto addFailure = [](CatalogPackage& package,
                std::string code, std::string message, const std::filesystem::path& location)
            {
                package.diagnostics.push_back({DiagnosticSeverity::Error,
                    std::move(code), std::move(message), location.string()});
            };

            for (auto& package : scan.packages)
            {
                auto module = std::make_shared<NativeModule>();
                module->packageRoot = package.packageRoot;
                module->manifest = package.manifest;
                module->manifestText = package.manifestText;
                module->extensionId = package.extensionId;
                module->library = LoadLibraryW(package.entryPath.c_str());
                if (module->library == nullptr)
                {
                    addFailure(package, "EXTENSION_LOAD_FAILED",
                        "LoadLibrary failed for the extension entry.", package.entryPath);
                    continue;
                }

                const auto query = reinterpret_cast<ARTestExtensionQueryFn>(
                    GetProcAddress(module->library, "ARTestExtension_Query"));
                if (query == nullptr)
                {
                    addFailure(package, "EXTENSION_QUERY_MISSING",
                        "The extension query export is missing.", package.entryPath);
                    continue;
                }

                module->api.struct_size = sizeof(ARTestExtensionApiV0);
                ErrorStorage queryError;
                auto status = query(
                    ARTEST_EXTENSION_ABI_MAJOR, ARTEST_EXTENSION_ABI_MINOR,
                    &module->api, &queryError.buffer);
                if (status != ARTEST_STATUS_OK
                    || module->api.abi_major != ARTEST_EXTENSION_ABI_MAJOR
                    || module->api.abi_minor > ARTEST_EXTENSION_ABI_MINOR
                    || module->api.create_extension == nullptr
                    || module->api.destroy_extension == nullptr
                    || module->api.get_component_type_count == nullptr
                    || module->api.get_component_descriptor == nullptr
                    || module->api.create_component == nullptr
                    || module->api.destroy_component == nullptr
                    || module->api.invoke_component == nullptr)
                {
                    addFailure(package, "EXTENSION_ABI_INVALID",
                        queryError.Message("The extension function table is invalid."),
                        package.entryPath);
                    continue;
                }
                if (ToString(module->api.extension_id) != module->extensionId)
                {
                    addFailure(package, "EXTENSION_ID_MISMATCH",
                        "The manifest and binary extension IDs differ.",
                        package.manifestPath);
                    continue;
                }

                const auto manifestPayload = JsonPayload(module->manifestText);
                ErrorStorage createError;
                status = module->api.create_extension(
                    &m_implementation->hostApi, &manifestPayload,
                    &module->extension, &createError.buffer);
                if (status != ARTEST_STATUS_OK || module->extension == nullptr)
                {
                    addFailure(package, "EXTENSION_CREATE_FAILED",
                        createError.Message("The extension could not be created."),
                        package.entryPath);
                    continue;
                }

                const auto count = module->api.get_component_type_count(module->extension);
                if (count != module->manifest["components"].size())
                {
                    addFailure(package, "EXTENSION_COMPONENT_COUNT_MISMATCH",
                        "The manifest and binary component counts differ.",
                        package.manifestPath);
                    continue;
                }

                bool descriptorFailure = false;
                for (std::size_t index = 0; index < count; ++index)
                {
                    ARTestComponentDescriptorV0 descriptor{};
                    descriptor.struct_size = sizeof(descriptor);
                    ErrorStorage descriptorError;
                    status = module->api.get_component_descriptor(
                        module->extension, index, &descriptor, &descriptorError.buffer);
                    const auto& declared = module->manifest["components"][index];
                    ComponentRecord record{
                        ParseKind(declared.value("kind", std::string{})),
                        ARTEST_COMPONENT_FLAG_NONE,
                        declared.value("typeId", std::string{}),
                        declared.value("contractId", std::string{}),
                        declared.value("version", std::string{}),
                        declared.value("displayName", std::string{})};
                    if (declared.contains("flags") && declared["flags"].is_array())
                    {
                        for (const auto& flag : declared["flags"])
                        {
                            if (flag == "simulated")
                                record.flags |= ARTEST_COMPONENT_FLAG_SIMULATED;
                            if (flag == "requiresHardware")
                                record.flags |= ARTEST_COMPONENT_FLAG_REQUIRES_HARDWARE;
                        }
                    }
                    if (status != ARTEST_STATUS_OK
                        || descriptor.kind != record.kind
                        || ToString(descriptor.type_id) != record.typeId
                        || ToString(descriptor.contract_id) != record.contractId)
                    {
                        addFailure(package, "EXTENSION_DESCRIPTOR_MISMATCH",
                            descriptorError.Message(
                                "The manifest and binary descriptor differ."),
                            package.manifestPath);
                        descriptorFailure = true;
                        break;
                    }
                    const auto typeKey = record.typeId;
                    module->components.push_back(record);
                    types.emplace(typeKey,
                        std::make_pair(module, std::move(record)));
                }
                if (!descriptorFailure) loaded.push_back(std::move(module));
            }
            if (!scan.IsValid()) return reject();

            // Registration is committed only after every package and descriptor passes.
            // Preflight prevents the append-only registries from observing partial catalogs.
            for (const auto& [typeId, entry] : types)
            {
                if ((entry.second.kind == ARTEST_COMPONENT_KIND_COMMAND
                        && commands.Contains(typeId))
                    || (entry.second.kind == ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER
                        && instruments.Contains(typeId)))
                {
                    scan.diagnostics.push_back({DiagnosticSeverity::Error,
                        "EXTENSION_COMPONENT_DUPLICATE",
                        "The component type conflicts with an existing registration.",
                        typeId});
                }
            }
            if (!scan.IsValid()) return reject();

            const auto self = shared_from_this();
            std::vector<std::string> registeredCommands;
            std::vector<std::string> registeredInstruments;
            const auto rollbackRegistrations = [&]() noexcept
            {
                for (auto item = registeredCommands.rbegin();
                    item != registeredCommands.rend(); ++item)
                    static_cast<void>(commands.Unregister(*item));
                for (auto item = registeredInstruments.rbegin();
                    item != registeredInstruments.rend(); ++item)
                    static_cast<void>(instruments.Unregister(*item));
            };
            try
            {
                for (const auto& [typeId, entry] : types)
                {
                    OperationResult registration;
                    if (entry.second.kind == ARTEST_COMPONENT_KIND_COMMAND)
                    {
                        registration = commands.Register(typeId, [self, typeId]
                        {
                            return std::make_unique<NativeCommandAdapter>(self, typeId);
                        });
                        if (registration.Succeeded())
                            registeredCommands.push_back(typeId);
                    }
                    else if (entry.second.kind == ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER)
                    {
                        registration = instruments.Register(typeId,
                            [self, typeId](IEventSink&)
                            {
                                return std::make_unique<NativeInstrumentAdapter>(self, typeId);
                            });
                        if (registration.Succeeded())
                            registeredInstruments.push_back(typeId);
                    }
                    if (!registration.Succeeded())
                    {
                        scan.diagnostics.insert(scan.diagnostics.end(),
                            registration.diagnostics.begin(), registration.diagnostics.end());
                        break;
                    }
                }
            }
            catch (...)
            {
                rollbackRegistrations();
                throw;
            }
            if (!scan.IsValid())
            {
                rollbackRegistrations();
                return reject();
            }

            m_implementation->modules = std::move(loaded);
            m_implementation->types = std::move(types);
            m_implementation->lastScan = std::move(scan);
            m_implementation->catalogStatus = "active";
            ++m_implementation->catalogGeneration;
            m_implementation->eventSink.Publish({
                EngineEventKind::Diagnostic, EngineEventSeverity::Information,
                "extension-catalog",
                "Native extension catalog validated and activated atomically."});
            return OperationResult::Success();
        }
        catch (const std::exception& exception)
        {
            scan.diagnostics.push_back({DiagnosticSeverity::Error,
                "EXTENSION_CATALOG_EXCEPTION", exception.what(),
                approvedRoot.string()});
            return reject();
        }
    }

    nlohmann::json NativeExtensionRuntime::CatalogSnapshot() const
    {
        std::scoped_lock lock{m_implementation->catalogMutex};
        nlohmann::json active = nlohmann::json::array();
        for (const auto& module : m_implementation->modules)
            active.push_back(module->manifest);
        return m_implementation->lastScan.ToJson(
            m_implementation->catalogStatus,
            m_implementation->catalogGeneration,
            active);
    }

    ValueResult<std::shared_ptr<NativeComponentInstance>>NativeExtensionRuntime::CreateComponent(const std::string& typeId, const nlohmann::json& configuration)
    {
        ValueResult<std::shared_ptr<NativeComponentInstance>> result;
        const auto found = m_implementation->types.find(typeId);
        if (found == m_implementation->types.end())
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error, "EXTENSION_COMPONENT_UNKNOWN",
                "Unknown extension component type: " + typeId, typeId});
            return result;
        }
        const auto text = configuration.dump();
        const auto payload = JsonPayload(text);

        ARTestComponentHandle handle = nullptr;
        ErrorStorage error;
        ARTestStatus status;
        {
            std::scoped_lock lock{found->second.first->invocationMutex};
            status = found->second.first->api.create_component(found->second.first->extension, View(typeId), &payload, &handle, &error.buffer);
        }

        if (status != ARTEST_STATUS_OK || handle == nullptr)
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "EXTENSION_COMPONENT_CREATE_FAILED",
                error.Message("The extension component could not be created."), typeId});
            return result;
        }

        result.value = std::make_shared<NativeComponentInstance>(found->second.first, found->second.second, handle);
        return result;
    }

    OperationResult NativeExtensionRuntime::Invoke(
        const std::shared_ptr<NativeComponentInstance>& component,
        const std::string& operationId,
        const nlohmann::json& request,
        const CancellationToken* cancellation,
        nlohmann::json* response)
    {
        if (!component)
            return OperationResult::Failure(
                "EXTENSION_COMPONENT_INVALID",
                "A valid extension component is required.");
        const auto text = request.dump();
        const auto payload = JsonPayload(text);
        struct Capture { std::string text; } capture;
        const auto write = [](void* context, const ARTestPayloadView* value,
            ARTestErrorBuffer*) noexcept -> ARTestStatus
        {
            if (context == nullptr || value == nullptr
                || value->encoding != ARTEST_PAYLOAD_ENCODING_JSON_UTF8)
                return ARTEST_STATUS_INVALID_ARGUMENT;
            static_cast<Capture*>(context)->text.assign(
                reinterpret_cast<const char*>(value->bytes.data), value->bytes.size);
            return ARTEST_STATUS_OK;
        };
        ARTestResultSinkV0 sink{sizeof(ARTestResultSinkV0), 0U, &capture, write};
        const auto cancelled = [](void* context) noexcept -> ARTestBool32
        {
            const auto* token = static_cast<const CancellationToken*>(context);
            return token != nullptr && token->IsCancellationRequested()
                ? ARTEST_TRUE : ARTEST_FALSE;
        };
        ARTestInvocationContextV0 invocation{
            sizeof(ARTestInvocationContextV0), 0U, 1U, 0U,
            const_cast<CancellationToken*>(cancellation), cancelled};
        ErrorStorage error;
        ARTestStatus status;
        {
            std::scoped_lock lock{component->module->invocationMutex};
            status = component->module->api.invoke_component(
                component->module->extension, component->handle,
                View(operationId), &payload, &invocation, &sink, &error.buffer);
        }
        if (status != ARTEST_STATUS_OK)
            return OperationResult::Failure(
                status == ARTEST_STATUS_CANCELLED ? "EXTENSION_CANCELLED"
                    : status == ARTEST_STATUS_TIMED_OUT ? "EXTENSION_TIMED_OUT"
                    : "EXTENSION_INVOCATION_FAILED",
                error.Message("The extension invocation failed."), operationId);
        if (response != nullptr && !capture.text.empty())
        {
            try { *response = nlohmann::json::parse(capture.text); }
            catch (const std::exception& exception)
            {
                return OperationResult::Failure(
                    "EXTENSION_RESULT_INVALID", exception.what(), operationId);
            }
        }
        return OperationResult::Success();
    }

    OperationResult NativeExtensionRuntime::RegisterService(
        std::string instanceId,
        const std::shared_ptr<NativeComponentInstance>& component)
    {
        if (instanceId.empty() || !component)
            return OperationResult::Failure(
                "EXTENSION_SERVICE_INVALID",
                "Service instance ID and component are required.");
        std::scoped_lock lock{m_implementation->serviceMutex};
        if (m_implementation->services.contains(instanceId))
            return OperationResult::Failure(
                "EXTENSION_SERVICE_DUPLICATE",
                "The service instance ID is already active.", instanceId);
        m_implementation->services.emplace(std::move(instanceId), component);
        return OperationResult::Success();
    }

    void NativeExtensionRuntime::UnregisterService(
        const std::string& instanceId) noexcept
    {
        std::scoped_lock lock{m_implementation->serviceMutex};
        m_implementation->services.erase(instanceId);
    }
}

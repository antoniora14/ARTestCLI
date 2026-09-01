#include "NativeExtensionRuntime.h"

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
    constexpr std::uintmax_t MaximumManifestSize = 1024U * 1024U;
    constexpr const char* ManifestName = "artest-extension.json";

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

    [[nodiscard]] bool IsContained(const std::filesystem::path& root, const std::filesystem::path& path)
    {
        const auto relative = path.lexically_relative(root);
        return !relative.empty()
            && relative.native().find(L"..") != 0U
            && !relative.is_absolute();
    }

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
        bool registered = false;
    };

    NativeExtensionRuntime::NativeExtensionRuntime(IEventSink& eventSink)
        : m_implementation(std::make_unique<Implementation>(eventSink)) {}
    NativeExtensionRuntime::~NativeExtensionRuntime() = default;

    OperationResult NativeExtensionRuntime::Refresh(const std::filesystem::path& approvedRoot)
    {
        OperationResult result;
        try
        {
            if (!m_implementation->modules.empty())
                return OperationResult::Failure("EXTENSION_CATALOG_ALREADY_LOADED", "D1 catalogs are immutable after the first successful refresh.");
            const auto root = std::filesystem::weakly_canonical(approvedRoot);
            if (!std::filesystem::is_directory(root))
                return OperationResult::Failure("EXTENSION_ROOT_INVALID", "The approved extension root is not a directory.", root.string());

            std::vector<std::shared_ptr<NativeModule>> loaded;
            std::unordered_map<std::string, std::pair<std::shared_ptr<NativeModule>, ComponentRecord>> types;
            for (const auto& entry : std::filesystem::directory_iterator(root))
            {
                if (!entry.is_directory()) continue;
                const auto manifestPath = entry.path() / ManifestName;
                if (!std::filesystem::is_regular_file(manifestPath)) continue;
                if (std::filesystem::file_size(manifestPath) > MaximumManifestSize)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_MANIFEST_TOO_LARGE",
                        "The manifest exceeds the 1 MB limit.", manifestPath.string()});
                    continue;
                }

                std::ifstream input{manifestPath, std::ios::binary};
                std::ostringstream text;
                text << input.rdbuf();
                auto manifest = nlohmann::json::parse(text.str());
                if (!manifest.is_object()
                    || manifest.value("schemaVersion", 0) != 1
                    || !manifest.contains("extensionId")
                    || !manifest.contains("version")
                    || !manifest.contains("runtime")
                    || !manifest.contains("components")
                    || !manifest["components"].is_array())
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_MANIFEST_INVALID",
                        "Required manifest fields are missing.", manifestPath.string()});
                    continue;
                }

                const auto& runtime = manifest["runtime"];
                if (runtime.value("kind", std::string{}) != "native"
                    || runtime.value("isolation", std::string{}) != "inProcess"
                    || runtime.value("architecture", std::string{}) != "x64"
                    || !runtime.contains("abi")
                    || runtime["abi"].value("major", 999U) != ARTEST_EXTENSION_ABI_MAJOR
                    || runtime["abi"].value("minor", 999U) > ARTEST_EXTENSION_ABI_MINOR)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_RUNTIME_INCOMPATIBLE",
                        "The native runtime or ABI is not compatible.", manifestPath.string()});
                    continue;
                }

                const auto packageRoot = std::filesystem::weakly_canonical(entry.path());
                const auto libraryPath = std::filesystem::weakly_canonical(
                    packageRoot / std::filesystem::path{
                        runtime.value("entry", std::string{})});

                if (!IsContained(packageRoot, libraryPath) || !std::filesystem::is_regular_file(libraryPath))
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_ENTRY_INVALID",
                        "The native entry must be a file inside its package.",
                        manifestPath.string()});
                    continue;
                }

                auto module = std::make_shared<NativeModule>();
                module->packageRoot = packageRoot;
                module->manifest = manifest;
                module->manifestText = text.str();
                module->extensionId = manifest["extensionId"].get<std::string>();
                module->library = LoadLibraryW(libraryPath.c_str());
                if (module->library == nullptr)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_LOAD_FAILED",
                        "LoadLibrary failed for the extension entry.", libraryPath.string()});
                    continue;
                }
                const auto query = reinterpret_cast<ARTestExtensionQueryFn>(
                    GetProcAddress(module->library, "ARTestExtension_Query"));
                if (query == nullptr)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_QUERY_MISSING",
                        "The extension query export is missing.", libraryPath.string()});
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
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_ABI_INVALID",
                        queryError.Message("The extension function table is invalid."),
                        libraryPath.string()});
                    continue;
                }
                if (ToString(module->api.extension_id) != module->extensionId)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_ID_MISMATCH",
                        "The manifest and binary extension IDs differ.", manifestPath.string()});
                    continue;
                }

                const auto manifestPayload = JsonPayload(module->manifestText);
                ErrorStorage createError;
                status = module->api.create_extension(
                    &m_implementation->hostApi, &manifestPayload,
                    &module->extension, &createError.buffer);
                if (status != ARTEST_STATUS_OK || module->extension == nullptr)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error, "EXTENSION_CREATE_FAILED",
                        createError.Message("The extension could not be created."),
                        libraryPath.string()});
                    continue;
                }

                const auto count =
                    module->api.get_component_type_count(module->extension);
                if (count != manifest["components"].size())
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error,
                        "EXTENSION_COMPONENT_COUNT_MISMATCH",
                        "The manifest and binary component counts differ.",
                        manifestPath.string()});
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
                    const auto& declared = manifest["components"][index];
                    ComponentRecord record{
                        ParseKind(declared.value("kind", std::string{})),
                        ARTEST_COMPONENT_FLAG_NONE,
                        declared.value("typeId", std::string{}),
                        declared.value("contractId", std::string{}),
                        declared.value("version", std::string{}),
                        declared.value("displayName", std::string{})};
                    if (declared.contains("flags"))
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
                        result.diagnostics.push_back({
                            DiagnosticSeverity::Error,
                            "EXTENSION_DESCRIPTOR_MISMATCH",
                            descriptorError.Message(
                                "The manifest and binary descriptor differ."),
                            manifestPath.string()});
                        descriptorFailure = true;
                        break;
                    }
                    if (types.contains(record.typeId))
                    {
                        result.diagnostics.push_back({
                            DiagnosticSeverity::Error,
                            "EXTENSION_COMPONENT_DUPLICATE",
                            "A component type ID is registered by more than one package.",
                            manifestPath.string()});
                        descriptorFailure = true;
                        break;
                    }
                    const auto typeKey = record.typeId;
                    module->components.push_back(record);
                    types.emplace(
                        typeKey, std::make_pair(module, std::move(record)));
                }
                if (!descriptorFailure) loaded.push_back(std::move(module));
            }
            if (ContainsErrors(result.diagnostics)) return result;
            if (loaded.empty())
                return OperationResult::Failure(
                    "EXTENSION_CATALOG_EMPTY",
                    "No valid extension packages were found.", root.string());
            m_implementation->modules = std::move(loaded);
            m_implementation->types = std::move(types);
            m_implementation->eventSink.Publish({
                EngineEventKind::Diagnostic, EngineEventSeverity::Information,
                "extension-catalog", "Native extension catalog loaded."});
            return result;
        }
        catch (const std::exception& exception)
        {
            return OperationResult::Failure(
                "EXTENSION_CATALOG_EXCEPTION", exception.what(),
                approvedRoot.string());
        }
    }

    OperationResult NativeExtensionRuntime::RegisterComponents(CommandRegistry& commands, InstrumentRegistry& instruments)
    {
        if (m_implementation->registered)
            return OperationResult::Failure(
                "EXTENSION_COMPONENTS_ALREADY_REGISTERED",
                "Extension components were already registered.");
        OperationResult result;
        const auto self = shared_from_this();
        for (const auto& [typeId, entry] : m_implementation->types)
        {
            if ((entry.second.kind == ARTEST_COMPONENT_KIND_COMMAND
                    && commands.Contains(typeId))
                || (entry.second.kind == ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER
                    && instruments.Contains(typeId)))
            {
                return OperationResult::Failure(
                    "EXTENSION_COMPONENT_DUPLICATE",
                    "The component type conflicts with an existing registration.",
                    typeId);
            }
        }
        for (const auto& [typeId, entry] : m_implementation->types)
        {
            OperationResult registration;
            if (entry.second.kind == ARTEST_COMPONENT_KIND_COMMAND)
            {
                registration = commands.Register(typeId, [self, typeId]
                {
                    return std::make_unique<NativeCommandAdapter>(self, typeId);
                });
            }
            else if (entry.second.kind == ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER)
            {
                registration = instruments.Register(typeId, [self, typeId](IEventSink&)
                {
                    return std::make_unique<NativeInstrumentAdapter>(self, typeId);
                });
            }
            if (!registration.Succeeded())
                result.diagnostics.insert(
                    result.diagnostics.end(),
                    registration.diagnostics.begin(), registration.diagnostics.end());
        }
        if (result.Succeeded()) m_implementation->registered = true;
        return result;
    }

    nlohmann::json NativeExtensionRuntime::CatalogSnapshot() const
    {
        nlohmann::json snapshot{
            {"schema", "artest.schema.extension-catalog.v1"},
            {"abi", {
                {"major", ARTEST_EXTENSION_ABI_MAJOR},
                {"minor", ARTEST_EXTENSION_ABI_MINOR}}},
            {"extensions", nlohmann::json::array()}};
        for (const auto& module : m_implementation->modules)
            snapshot["extensions"].push_back(module->manifest);
        return snapshot;
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
            status = found->second.first->api.create_component(
                found->second.first->extension, View(typeId),
                &payload, &handle, &error.buffer);
        }
        if (status != ARTEST_STATUS_OK || handle == nullptr)
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "EXTENSION_COMPONENT_CREATE_FAILED",
                error.Message("The extension component could not be created."), typeId});
            return result;
        }
        result.value = std::make_shared<NativeComponentInstance>(
            found->second.first, found->second.second, handle);
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

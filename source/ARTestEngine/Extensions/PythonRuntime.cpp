#include "PythonRuntime.h"
#include "ManagedIntegrity.h"
#include "FileIntegrity.h"
#include <chrono>
#include <fstream>
namespace artest::extensions
{
namespace wire = process::wire;
struct PythonWorker
{
    std::unique_ptr<process::WorkerSupervisor> supervisor;
    NativeServiceBroker *broker = nullptr;
    std::map<std::uint64_t, ARTestServiceHandle> services;
    std::uint64_t nextService = 1;
    ~PythonWorker()
    {
        for (const auto &[id, handle] : services) { (void)id; NativeServiceBroker::ReleaseService(broker, handle); }
    }
};
namespace
{
wire::Request Request(wire::Operation operation, std::uint64_t handle, const nlohmann::json &data)
{
    wire::Request request;
    request.set_operation(operation);
    request.set_component(handle);
    request.mutable_payload()->set_schema_id("artest.schema.generic-json.v1");
    request.mutable_payload()->set_json(data.dump());
    return request;
}
void RequireOk(const wire::Response &response)
{
    if (response.status() != wire::OK) throw std::runtime_error(response.diagnostic());
}
ARTestStatus NativeStatus(wire::Status status)
{
    switch (status)
    {
    case wire::OK: return ARTEST_STATUS_OK;
    case wire::CANCELLED: return ARTEST_STATUS_CANCELLED;
    case wire::TIMED_OUT: return ARTEST_STATUS_TIMED_OUT;
    case wire::INVALID_ARGUMENT: return ARTEST_STATUS_INVALID_ARGUMENT;
    case wire::NOT_FOUND: return ARTEST_STATUS_NOT_FOUND;
    default: return ARTEST_STATUS_EXTENSION_FAILURE;
    }
}
wire::Status WireStatus(ARTestStatus status)
{
    switch (status)
    {
    case ARTEST_STATUS_OK: return wire::OK;
    case ARTEST_STATUS_CANCELLED: return wire::CANCELLED;
    case ARTEST_STATUS_TIMED_OUT: return wire::TIMED_OUT;
    case ARTEST_STATUS_INVALID_ARGUMENT: return wire::INVALID_ARGUMENT;
    case ARTEST_STATUS_NOT_FOUND: return wire::NOT_FOUND;
    default: return wire::EXTENSION_FAILURE;
    }
}
nlohmann::json ExpectedDescriptor(const CatalogPackage &package)
{
    auto components = package.manifest.at("components");
    for (auto &component : components)
    {
        const auto binding = component.at("schemas").at(0);
        std::ifstream stream(package.packageRoot / binding.at("path").get<std::string>());
        component["schema"] = nlohmann::json::parse(stream);
        component["schemaRole"] = binding.at("role");
        component["schemaId"] = binding.at("schemaId");
        component.erase("schemas");
    }
    return {{"extensionId", package.extensionId}, {"version", package.version},
            {"displayName", package.descriptor.displayName}, {"publisher", package.descriptor.publisher},
            {"components", std::move(components)}};
}
}
PythonComponent::~PythonComponent()
{
    if (!worker || worker->supervisor->State() != process::WorkerState::Ready) return;
    try { RequireOk(worker->supervisor->Call(Request(wire::DESTROY, handle, {}), std::chrono::seconds{2})); }
    catch (...) { worker->supervisor->Stop(); }
}
void PythonRuntime::Configure(nlohmann::json environments)
{
    if (!environments.is_object()) throw std::invalid_argument("pythonEnvironments must be an object.");
    m_environments = std::move(environments);
}
void PythonRuntime::Load(const CatalogScan &scan)
{
    // Failed activation may be retried with a corrected catalog on the same Engine.
    if (!m_workers.empty()) (void)EndSession();
    m_packages.clear();
    m_types.clear();
    for (const auto &package : scan.packages)
    {
        if (package.descriptor.runtime.kind != "python") continue;
        m_packages.emplace(package.extensionId, package);
        for (const auto &component : package.descriptor.components)
            m_types.emplace(component.typeId, std::make_pair(package.extensionId, component.contractId));
        (void)Worker(package.extensionId);
    }
}
std::shared_ptr<PythonWorker> PythonRuntime::Worker(const std::string &id)
{
    const auto found = m_workers.find(id);
    if (found != m_workers.end()) return found->second;
    const auto &package = m_packages.at(id);
    const auto started = std::chrono::steady_clock::now();
    const auto phase = [this, &id, started](const char *name) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        m_events.Publish({EngineEventKind::Diagnostic, EngineEventSeverity::Information, id,
            std::string{"PYTHON_ACTIVATION phase="} + name +
                " elapsedMs=" + std::to_string(elapsed)});
    };
    phase("package-validation-begin");
    (void)ValidateManagedPackage(package);
    phase("package-validation-end");
    if (!m_environments.contains(id)) throw std::runtime_error("PYTHON_ENVIRONMENT_REQUIRED: " + id);
    phase("environment-validation-begin");
    const auto environment = ValidatePythonEnvironment(package, m_environments.at(id).get<std::string>());
    phase("environment-validation-end");
    process::WorkerOptions options;
    options.executable = environment.interpreter;
    options.arguments = {L"-I", L"-B", L"-S", environment.launcher.wstring(), L"--package", package.packageRoot.wstring()};
    options.extensionId = id;
    options.fingerprint = Sha256(package.manifestPath);
    auto worker = std::make_shared<PythonWorker>();
    worker->broker = &m_broker;
    worker->supervisor = std::make_unique<process::WorkerSupervisor>(std::move(options));
    worker->supervisor->SetServiceHandler([this, weak = std::weak_ptr<PythonWorker>(worker)](
        const wire::Request &request, process::WorkerSupervisor &) { return Service(request, *weak.lock()); });
    worker->supervisor->SetEventHandler([this](const wire::Event &event) {
        m_events.Publish({EngineEventKind::Diagnostic, event.severity() == 2 ? EngineEventSeverity::Error :
            event.severity() == 1 ? EngineEventSeverity::Warning : EngineEventSeverity::Information,
            event.category(), event.message()});
    });
    phase("worker-start-begin");
    worker->supervisor->Start();
    phase("worker-start-end");
    phase("descriptor-request-begin");
    const auto description = worker->supervisor->Call(Request(wire::DESCRIBE, 0, {}), std::chrono::seconds{5});
    phase("descriptor-request-end");
    RequireOk(description);
    if (nlohmann::json::parse(description.payload().json()) != ExpectedDescriptor(package))
        throw std::runtime_error("PYTHON_DESCRIPTOR_MISMATCH: regenerate the package.");
    m_workers.emplace(id, worker);
    m_events.Publish({EngineEventKind::Diagnostic, EngineEventSeverity::Information, id, "PYTHON_WORKER_READY"});
    return worker;
}
std::shared_ptr<ComponentLease> PythonRuntime::Create(const std::string &type, const nlohmann::json &configuration)
{
    auto worker = Worker(m_types.at(type).first);
    auto request = Request(wire::CREATE, 0, configuration);
    request.set_type_id(type);
    const auto response = worker->supervisor->Call(request, std::chrono::seconds{5});
    RequireOk(response);
    if (!response.handle()) throw std::runtime_error("Python returned an invalid component handle.");
    auto lease = std::make_shared<PythonComponent>();
    lease->worker = std::move(worker);
    lease->handle = response.handle();
    lease->contract = m_types.at(type).second;
    return lease;
}
ARTestStatus PythonRuntime::Invoke(const std::shared_ptr<PythonComponent> &component, ARTestStringView operation,
    const ARTestPayloadView *payload, const ARTestInvocationContextV0 *invocation,
    const ARTestResultSinkV0 *sink, ARTestErrorBuffer *error) noexcept
{
    const auto previous = m_invocation;
    m_invocation = invocation;
    struct Scope { const ARTestInvocationContextV0 *&slot; const ARTestInvocationContextV0 *old; ~Scope() { slot = old; } } scope{m_invocation, previous};
    try
    {
        const auto id = ToString(operation);
        const auto operationKind = id == "artest.lifecycle.initialize.v1" ? wire::INITIALIZE :
            id == "artest.lifecycle.shutdown.v1" ? wire::SHUTDOWN : wire::INVOKE;
        auto request = Request(operationKind, component->handle, nlohmann::json::object());
        request.set_operation_id(id);
        if (payload)
        {
            request.mutable_payload()->set_schema_id(ToString(payload->schema_id));
            request.mutable_payload()->set_json(reinterpret_cast<const char*>(payload->bytes.data), payload->bytes.size);
        }
        auto timeout = std::chrono::milliseconds{operationKind == wire::INVOKE ? 300000 : 5000};
        if (invocation && invocation->deadline_monotonic_ns)
        {
            const auto now = NativeServiceBroker::MonotonicTime(nullptr);
            timeout = invocation->deadline_monotonic_ns <= now ? std::chrono::milliseconds{0} :
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::nanoseconds{invocation->deadline_monotonic_ns - now});
        }
        const auto response = component->worker->supervisor->Call(request, timeout, [invocation] {
            return invocation && invocation->is_cancellation_requested &&
                invocation->is_cancellation_requested(invocation->cancellation_context);
        });
        if (response.status() != wire::OK)
        {
            SetError(error, response.diagnostic());
            return NativeStatus(response.status()) |
                (response.effect_indeterminate() ? ARTEST_STATUS_EFFECT_INDETERMINATE_FLAG : 0);
        }
        if (sink && !response.payload().json().empty())
        {
            auto output = JsonPayload(response.payload().json());
            output.schema_id = View(response.payload().schema_id());
            return sink->write(sink->sink_context, &output, error);
        }
        return ARTEST_STATUS_OK;
    }
    catch (const process::ProcessError &exception)
    {
        SetError(error, std::string{"EXTENSION_OUTCOME_INDETERMINATE: "} + exception.what());
        return ARTEST_STATUS_EXTENSION_FAILURE | ARTEST_STATUS_EFFECT_INDETERMINATE_FLAG;
    }
    catch (const std::exception &exception) { SetError(error, exception.what()); return ARTEST_STATUS_EXTENSION_FAILURE; }
}
wire::Response PythonRuntime::Service(const wire::Request &request, PythonWorker &worker)
{
    ErrorStorage error;
    if (request.operation() == wire::RESOLVE_SERVICE)
    {
        const auto payload = nlohmann::json::parse(request.payload().json());
        const auto instance = payload.at("instanceId").get<std::string>();
        ARTestServiceHandle handle = nullptr;
        const auto status = NativeServiceBroker::ResolveService(&m_broker, View(request.operation_id()),
            View(instance), &handle, &error.buffer);
        auto response = process::Response(WireStatus(status), error.Message(""));
        if (status == ARTEST_STATUS_OK)
        {
            const auto token = worker.nextService++;
            worker.services.emplace(token, handle);
            response.set_handle(token);
        }
        return response;
    }
    const auto found = worker.services.find(request.component());
    if (found == worker.services.end()) return process::Response(wire::NOT_FOUND, "Unknown session service lease.");
    if (request.operation() == wire::RELEASE_SERVICE)
    {
        NativeServiceBroker::ReleaseService(&m_broker, found->second);
        worker.services.erase(found);
        return process::Response(wire::OK);
    }
    if (request.operation() != wire::INVOKE_SERVICE) return process::Response(wire::INVALID_ARGUMENT);
    wire::Response response;
    const auto capture = [](void *context, const ARTestPayloadView *payload, ARTestErrorBuffer *) noexcept -> ARTestStatus {
        try
        {
            auto &result = *static_cast<wire::Response*>(context);
            if (result.has_payload() || !payload || payload->bytes.size > process::MaxFrameBytes ||
                payload->encoding != ARTEST_PAYLOAD_ENCODING_JSON_UTF8) return ARTEST_STATUS_INVALID_ARGUMENT;
            result.mutable_payload()->set_schema_id(ToString(payload->schema_id));
            result.mutable_payload()->set_json(reinterpret_cast<const char*>(payload->bytes.data), payload->bytes.size);
            return ARTEST_STATUS_OK;
        }
        catch (...) { return ARTEST_STATUS_HOST_FAILURE; }
    };
    ARTestResultSinkV0 sink{sizeof(ARTestResultSinkV0), 0, &response, capture};
    auto payload = JsonPayload(request.payload().json());
    payload.schema_id = View(request.payload().schema_id());
    const auto status = NativeServiceBroker::InvokeService(&m_broker, found->second,
        View(request.operation_id()), &payload, m_invocation, &sink, &error.buffer);
    response.set_status(WireStatus(status & ~ARTEST_STATUS_EFFECT_INDETERMINATE_FLAG));
    response.set_effect_indeterminate((status & ARTEST_STATUS_EFFECT_INDETERMINATE_FLAG) != 0);
    response.set_diagnostic(error.Message(""));
    return response;
}
OperationResult PythonRuntime::EndSession()
{
    auto result = OperationResult::Success();
    for (const auto &[id, worker] : m_workers)
    {
        const bool leaked = !worker->services.empty();
        for (const auto &[token, handle] : worker->services)
        { (void)token; NativeServiceBroker::ReleaseService(&m_broker, handle); }
        worker->services.clear();
        worker->supervisor->Stop();
        if (leaked || worker->supervisor->State() != process::WorkerState::Exited)
            result.diagnostics.push_back({DiagnosticSeverity::Error, "PYTHON_CLEANUP_UNCONFIRMED",
                "Worker cleanup is unconfirmed; physical hardware state is unknown.", id});
    }
    m_workers.clear();
    return result;
}
}

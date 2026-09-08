#pragma once
#include "NativeServiceBroker.h"
#include "ExtensionCatalog.h"
#include "../../ARTestEngine.Process/WorkerSupervisor.h"

namespace artest::extensions
{
struct PythonWorker;
class PythonComponent final : public ComponentLease
{
  public:
    std::shared_ptr<PythonWorker> worker;
    std::uint64_t handle = 0;
    std::string contract;
    ~PythonComponent() override;
};
// Language backend only: Core owns retries, sequencing, verdicts and cleanup ordering.
class PythonRuntime
{
  public:
    PythonRuntime(NativeServiceBroker &broker, IEventSink &events) : m_broker(broker), m_events(events) {}
    void Configure(nlohmann::json environments);
    void Load(const CatalogScan &scan);
    bool Contains(const std::string &type) const { return m_types.contains(type); }
    std::shared_ptr<ComponentLease> Create(const std::string &type, const nlohmann::json &configuration);
    ARTestStatus Invoke(const std::shared_ptr<PythonComponent> &component, ARTestStringView operation,
        const ARTestPayloadView *request, const ARTestInvocationContextV0 *invocation,
        const ARTestResultSinkV0 *sink, ARTestErrorBuffer *error) noexcept;
    OperationResult EndSession();
    void BeginSession() { m_indeterminate = false; }
    bool Indeterminate() const noexcept { return m_indeterminate; }
  private:
    std::shared_ptr<PythonWorker> Worker(const std::string &package);
    process::wire::Response Service(const process::wire::Request &request, PythonWorker &worker);
    NativeServiceBroker &m_broker;
    IEventSink &m_events;
    nlohmann::json m_environments = nlohmann::json::object();
    std::map<std::string, CatalogPackage> m_packages;
    std::map<std::string, std::pair<std::string, std::string>> m_types;
    std::map<std::string, std::shared_ptr<PythonWorker>> m_workers;
    const ARTestInvocationContextV0 *m_invocation = nullptr; // owner-thread, dynamically scoped
    bool m_indeterminate = false;
};
}

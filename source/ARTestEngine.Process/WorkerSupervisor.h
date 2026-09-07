#pragma once
#include "WorkerLaunch.h"
#include <functional>
#include <map>
#include <thread>

namespace artest::process
{
enum class WorkerState { Created, Starting, Handshaking, Ready, Stopping, Exited, Faulted };
struct WorkerOptions
{
    std::filesystem::path executable;
    std::vector<std::wstring> arguments;
    std::string extensionId, fingerprint;
    std::chrono::milliseconds startupTimeout{5000}, cancellationGrace{2000};
};

// Owner-thread API, including handlers, inspection and teardown. Call may reenter
// from a service handler; handlers must be bounded and must not block the pump.
// Process isolation cannot enforce deadlines on an in-process Engine callback.
class WorkerSupervisor
{
  public:
    using ServiceHandler = std::function<wire::Response(const wire::Request &, WorkerSupervisor &)>;
    using EventHandler = std::function<void(const wire::Event &)>;
    explicit WorkerSupervisor(WorkerOptions options);
    ~WorkerSupervisor();
    WorkerSupervisor(const WorkerSupervisor &) = delete;
    WorkerSupervisor &operator=(const WorkerSupervisor &) = delete;
    void Start();
    wire::Response Call(wire::Request request, std::chrono::milliseconds timeout,
                        std::function<bool()> cancelled = {});
    void Stop() noexcept;
    void SetServiceHandler(ServiceHandler handler) { m_serviceHandler = std::move(handler); }
    void SetEventHandler(EventHandler handler) { m_eventHandler = std::move(handler); }
    WorkerState State() const noexcept { return m_state; }
    DWORD ProcessId() const noexcept { return m_child.pid; }
    std::uint64_t Generation() const noexcept { return m_bootstrap.generation; }
    std::size_t DroppedEvents() const noexcept { return m_droppedEvents; }
  private:
    void Abort() noexcept;
    wire::Response Pump(std::uint64_t correlation, Clock::time_point deadline);
    bool CancellationRequested() const;
    WorkerOptions m_options;
    WorkerProcess m_child;
    Bootstrap m_bootstrap;
    std::unique_ptr<PipeChannel> m_channel;
    WorkerState m_state = WorkerState::Created;
    std::thread::id m_owner;
    std::uint64_t m_next = 2, m_lastService = 0, m_serviceParent = 0;
    std::vector<std::uint64_t> m_active;
    std::vector<Clock::time_point> m_deadlines;
    std::vector<std::function<bool()>> m_cancellations;
    std::vector<std::string> m_serviceAncestry;
    std::map<std::uint64_t, wire::Response> m_pending;
    ServiceHandler m_serviceHandler;
    EventHandler m_eventHandler;
    std::size_t m_droppedEvents = 0;
};
} // namespace artest::process

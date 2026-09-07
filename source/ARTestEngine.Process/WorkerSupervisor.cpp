#include "WorkerSupervisor.h"
#include <algorithm>
#include <cstring>

namespace artest::process
{
WorkerSupervisor::WorkerSupervisor(WorkerOptions options)
    : m_options(std::move(options)), m_owner(std::this_thread::get_id())
{
    m_active.reserve(16);
    m_deadlines.reserve(16);
    m_cancellations.reserve(16);
}
WorkerSupervisor::~WorkerSupervisor() { Stop(); }
void WorkerSupervisor::Abort() noexcept
{
    m_state = WorkerState::Faulted;
    if (m_child.job) TerminateJobObject(m_child.job.Get(), 137);
    if (m_child.process) WaitForSingleObject(m_child.process.Get(), 3000);
    m_channel.reset();
    m_pending.clear();
}
void WorkerSupervisor::Start()
{
    if (m_state != WorkerState::Created || m_owner != std::this_thread::get_id())
        throw ProcessError("PROCESS_STATE_INVALID", "Worker start requires its owner and Created state.");
    try
    {
        if (m_options.startupTimeout.count() <= 0 || m_options.cancellationGrace.count() <= 0)
            throw ProcessError("PROCESS_OPTIONS_INVALID", "Worker budgets must be positive.");
        m_state = WorkerState::Starting;
        m_bootstrap.nonce = RandomBytes(32);
        const auto name = Hex(RandomBytes(16));
        m_bootstrap.pipe = L"\\\\.\\pipe\\ARTest-" + std::wstring(name.begin(), name.end());
        m_bootstrap.extensionId = m_options.extensionId;
        m_bootstrap.fingerprint = m_options.fingerprint;
        const auto generation = RandomBytes(sizeof(m_bootstrap.generation));
        std::memcpy(&m_bootstrap.generation, generation.data(), generation.size());
        if (!m_bootstrap.generation) m_bootstrap.generation = 1;
        m_bootstrap.parentPid = GetCurrentProcessId();
        m_channel = std::make_unique<PipeChannel>(CreateLocalPipe(m_bootstrap.pipe));
        const auto deadline = Clock::now() + m_options.startupTimeout;
        m_child = LaunchWorker(m_options.executable, m_options.arguments, m_bootstrap);
        ConnectLocalPipe(m_channel->Handle(), m_child.process.Get(), deadline);
        ULONG client = 0;
        WinCheck(GetNamedPipeClientProcessId(m_channel->Handle(), &client) != FALSE, "Pipe peer identity");
        if (client != m_child.pid) throw ProcessError("PROCESS_PEER_INVALID", "Unexpected pipe client.");
        m_state = WorkerState::Handshaking;
        auto hello = m_channel->Read(deadline);
        if (!hello.has_hello() || hello.correlation() != 1 || hello.generation() != m_bootstrap.generation ||
            hello.hello().acknowledged() || hello.hello().extension_id() != m_bootstrap.extensionId ||
            hello.hello().fingerprint() != m_bootstrap.fingerprint || hello.hello().nonce() != m_bootstrap.nonce)
            throw ProcessError("PROCESS_HANDSHAKE_INVALID", "Worker identity/handshake mismatch.");
        hello.mutable_hello()->set_acknowledged(true);
        m_channel->Write(hello, deadline);
        m_state = WorkerState::Ready;
    }
    catch (...) { Abort(); throw; }
}
wire::Response WorkerSupervisor::Call(wire::Request request, std::chrono::milliseconds timeout,
                                      std::function<bool()> cancelled)
{
    if (m_owner != std::this_thread::get_id() || m_state != WorkerState::Ready)
        throw ProcessError("PROCESS_STATE_INVALID", "Worker calls are owner-thread operations in Ready state.");
    if (timeout.count() <= 0) return Response(wire::TIMED_OUT, "Invocation budget expired before dispatch.");
    if ((cancelled && cancelled()) || CancellationRequested())
        return Response(wire::CANCELLED, "Invocation cancelled before dispatch.");
    if (m_active.size() >= 16) throw ProcessError("PROCESS_REENTRANCY_LIMIT", "Service call depth exceeded.");
    auto deadline = Clock::now() + timeout;
    if (!m_deadlines.empty()) deadline = (std::min)(deadline, m_deadlines.back());
    const auto correlation = m_next;
    m_next += 2;
    m_active.push_back(correlation);
    m_deadlines.push_back(deadline);
    m_cancellations.push_back(std::move(cancelled));
    struct Scope
    {
        WorkerSupervisor &self;
        ~Scope() { self.m_active.pop_back(); self.m_deadlines.pop_back(); self.m_cancellations.pop_back(); }
    } scope{*this};
    try
    {
        auto message = Message(m_bootstrap.generation, correlation);
        message.set_parent(m_serviceParent);
        *message.mutable_request() = std::move(request);
        message.mutable_request()->set_has_deadline(true);
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) return Response(wire::TIMED_OUT, "Nested invocation budget expired.");
        message.mutable_request()->set_remaining_ms(static_cast<std::uint64_t>(left));
        m_channel->Write(message, (std::min)(deadline, Clock::now() + std::chrono::milliseconds{250}));
        return Pump(correlation, deadline);
    }
    catch (const ProcessError &error)
    {
        Abort();
        throw ProcessError(error.code, std::string(error.what()) +
            " Cleanup is unconfirmed; the hardware outcome may be indeterminate. No call was replayed.");
    }
    catch (...) { Abort(); throw; }
}
bool WorkerSupervisor::CancellationRequested() const
{
    return std::any_of(m_cancellations.begin(), m_cancellations.end(),
                       [](const auto &probe) { return probe && probe(); });
}
wire::Response WorkerSupervisor::Pump(std::uint64_t correlation, Clock::time_point deadline)
{
    bool cancelSent = false;
    auto interruptedStatus = wire::OK;
    auto grace = deadline;
    std::size_t events = 0;
    for (;;)
    {
        if (m_state != WorkerState::Ready || !m_channel)
            throw ProcessError("PROCESS_STATE_INVALID", "Worker stopped during a callback.");
        const auto now = Clock::now();
        if (!cancelSent && (now >= deadline || CancellationRequested()))
        {
            interruptedStatus = now >= deadline ? wire::TIMED_OUT : wire::CANCELLED;
            auto message = Message(m_bootstrap.generation, correlation);
            message.mutable_cancel();
            m_channel->Write(message, now + std::chrono::milliseconds{250});
            cancelSent = true;
            grace = now + m_options.cancellationGrace;
        }
        if (cancelSent && now >= grace)
            throw ProcessError("PROCESS_CANCEL_TIMEOUT", "Worker ignored cancellation/deadline.");
        const auto pending = m_pending.find(correlation);
        if (pending != m_pending.end())
        {
            auto response = std::move(pending->second);
            m_pending.erase(pending);
            return cancelSent ? Response(interruptedStatus, "Invocation interrupted. " + response.diagnostic()) : response;
        }
        wire::Envelope message;
        try { message = m_channel->Read(now + std::chrono::milliseconds{10}); }
        catch (const ProcessError &error)
        {
            if (error.code == "PROCESS_IO_TIMEOUT") continue;
            throw;
        }
        if (message.generation() != m_bootstrap.generation)
            throw ProcessError("PROCESS_GENERATION_INVALID", "Stale worker generation.");
        const auto active = std::find(m_active.begin(), m_active.end(), message.correlation()) != m_active.end();
        if (message.has_response())
        {
            if (!active || m_pending.contains(message.correlation()))
                throw ProcessError("PROCESS_CORRELATION_INVALID", "Duplicate or unknown terminal result.");
            m_pending.emplace(message.correlation(), message.response());
        }
        else if (message.has_cancel())
        {
            if (!active || !message.cancel().acknowledged() || !cancelSent ||
                message.correlation() != correlation)
                throw ProcessError("PROCESS_CANCEL_INVALID", "Unexpected cancellation acknowledgement.");
        }
        else if (message.has_event())
        {
            if (!active) throw ProcessError("PROCESS_CORRELATION_INVALID", "Event outside an active invocation.");
            if (++events <= 128) { if (m_eventHandler) m_eventHandler(message.event()); }
            else ++m_droppedEvents; // Control/results are never dropped with log overflow.
        }
        else if (message.has_request())
        {
            const auto &request = message.request();
            if (message.correlation() % 2 != 1 || message.correlation() <= m_lastService ||
                std::find(m_active.begin(), m_active.end(), message.parent()) == m_active.end() ||
                (request.operation() != wire::RESOLVE_SERVICE && request.operation() != wire::INVOKE_SERVICE &&
                 request.operation() != wire::RELEASE_SERVICE))
                throw ProcessError("PROCESS_SERVICE_INVALID", "Invalid scoped service request.");
            m_lastService = message.correlation();
            const auto key = std::to_string(request.component()) + ":" + request.operation_id();
            if (std::find(m_serviceAncestry.begin(), m_serviceAncestry.end(), key) != m_serviceAncestry.end())
                throw ProcessError("PROCESS_SERVICE_CYCLE", "Cyclic service call rejected.");
            const auto previous = m_serviceParent;
            m_serviceParent = message.correlation();
            m_serviceAncestry.push_back(key);
            struct Restore
            {
                WorkerSupervisor &self; std::uint64_t previous;
                ~Restore() { self.m_serviceParent = previous; self.m_serviceAncestry.pop_back(); }
            } restore{*this, previous};
            auto response = Message(m_bootstrap.generation, message.correlation());
            // Cancellation forbids new device work, but release remains available for cleanup.
            if (request.operation() != wire::RELEASE_SERVICE &&
                (cancelSent || Clock::now() >= deadline || CancellationRequested()))
                *response.mutable_response() = Response(
                    Clock::now() >= deadline ? wire::TIMED_OUT : wire::CANCELLED,
                    "Service invocation interrupted before dispatch.");
            else
                *response.mutable_response() = m_serviceHandler
                    ? m_serviceHandler(request, *this) : Response(wire::NOT_FOUND, "Service broker not configured.");
            if (m_state != WorkerState::Ready || !m_channel)
                throw ProcessError("PROCESS_STATE_INVALID", "Worker stopped during a service callback.");
            m_channel->Write(response, Clock::now() + std::chrono::milliseconds{250});
        }
        else throw ProcessError("PROCESS_PROTOCOL_INVALID", "Unexpected message after handshake.");
    }
}
void WorkerSupervisor::Stop() noexcept
{
    if (m_state == WorkerState::Created || m_state == WorkerState::Exited) return;
    if (m_state == WorkerState::Faulted) { Abort(); return; }
    if (!m_active.empty()) { Abort(); return; } // Never dismantle a live pump through a nested STOP call.
    try
    {
        wire::Request request;
        request.set_operation(wire::STOP);
        const auto response = Call(request, std::chrono::milliseconds{500});
        m_state = WorkerState::Stopping;
        if (response.status() != wire::OK || WaitForSingleObject(m_child.process.Get(), 1000) != WAIT_OBJECT_0)
        { Abort(); return; }
        m_channel.reset();
        // The process handle can signal before job accounting observes its exit.
        // Allow bounded accounting convergence without accepting a surviving descendant.
        const auto drainDeadline = Clock::now() + std::chrono::milliseconds{250};
        for (;;)
        {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if (!QueryInformationJobObject(m_child.job.Get(), JobObjectBasicAccountingInformation,
                                           &accounting, sizeof(accounting), nullptr))
            { Abort(); return; }
            if (accounting.ActiveProcesses == 0) break;
            if (Clock::now() >= drainDeadline) { Abort(); return; }
            Sleep(5);
        }
        m_child.job.Reset();
        m_state = WorkerState::Exited;
    }
    catch (...) { Abort(); }
}
} // namespace artest::process

#include "TreeProcess.h"
#include <QDir>
#include <QFileInfo>
#include <vector>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ARTestDev {
namespace {
void close(HANDLE &value) { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
QString quote(const QString &value) {
    QString out = "\"";
    int slashes = 0;
    for (QChar c : value) {
        if (c == '\\') { ++slashes; continue; }
        out += QString(slashes * (c == '"' ? 2 : 1), '\\');
        if (c == '"') out += '\\';
        out += c; slashes = 0;
    }
    return out + QString(slashes * 2, '\\') + '"';
}
}
struct TreeProcess::Native {
    HANDLE job = nullptr, process = nullptr, read = nullptr, input = nullptr, auditPort = nullptr;
    std::thread auditThread;
    ~Native() {
        // Kill-on-close also contains children if the owning window/application exits.
        if (job) TerminateJobObject(job, 1);
        if (auditPort && auditThread.joinable()) { PostQueuedCompletionStatus(auditPort, 0, 1, nullptr); auditThread.join(); }
        close(job); close(process); close(read); close(input); close(auditPort);
    }
};
TreeProcess::TreeProcess(QObject *parent) : QObject(parent) {
    poll_.setInterval(20);
    connect(&poll_, &QTimer::timeout, this, &TreeProcess::tick);
}
TreeProcess::~TreeProcess() = default;
bool TreeProcess::busy() const { return bool(native_); }
bool TreeProcess::start(const QString &program, const QStringList &arguments, const QString &cwd,
                        int timeoutMs, qsizetype outputLimit, bool commitProtocol) {
    if (busy() || !QDir::isAbsolutePath(program) || !QFileInfo(program).isFile() ||
        !QDir::isAbsolutePath(cwd) || timeoutMs < 1 || timeoutMs > 1800000 ||
        outputLimit < 1 || outputLimit > 1024 * 1024) return false;
    native_ = std::make_unique<Native>();
    result_ = {}; stoppingNow_ = false; reported_ = false;
    commitProtocol_ = commitProtocol; committing_ = false;
    timeoutMs_ = timeoutMs; limit_ = outputLimit;
    native_->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE write = nullptr, input = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process{};
    bool ok = native_->job && SetInformationJobObject(native_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
        CreatePipe(&native_->read, &write, &security, 0) && SetHandleInformation(native_->read, HANDLE_FLAG_INHERIT, 0);
    if (ok && audit_) {
        native_->auditPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{};
        association.CompletionPort = native_->auditPort;
        ok = native_->auditPort && SetInformationJobObject(native_->job, JobObjectAssociateCompletionPortInformation, &association, sizeof(association));
        if (ok) native_->auditThread = std::thread([this, port = native_->auditPort] {
            DWORD message = 0; ULONG_PTR key = 0; LPOVERLAPPED data = nullptr;
            while (GetQueuedCompletionStatus(port, &message, &key, &data, INFINITE)) {
                if (key == 1) break;
                if (message != JOB_OBJECT_MSG_NEW_PROCESS) continue;
                const DWORD id = DWORD(reinterpret_cast<ULONG_PTR>(data));
                HANDLE child = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
                wchar_t path[32768]{}; DWORD size = 32768;
                const bool identified = child && QueryFullProcessImageNameW(child, 0, path, &size);
                if (child) CloseHandle(child);
                emit processObserved(id, identified ? QString::fromWCharArray(path, int(size)) : QStringLiteral("unresolved"));
            }
        });
    }
    ok = ok && CreatePipe(&input, &native_->input, &security, 0) && SetHandleInformation(native_->input, HANDLE_FLAG_INHERIT, 0);
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> storage(size);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    const bool initialized = InitializeProcThreadAttributeList(attributes, 1, 0, &size) != FALSE;
    HANDLE inherited[]{write, input};
    ok = ok && input != INVALID_HANDLE_VALUE && initialized &&
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = write;
    startup.StartupInfo.hStdError = write;
    startup.lpAttributeList = attributes;
    QString command = quote(QDir::toNativeSeparators(program));
    for (const auto &argument : arguments) command += ' ' + quote(argument);
    std::wstring mutableCommand = command.toStdWString();
    const auto executable = QDir::toNativeSeparators(program).toStdWString();
    const auto directory = QDir::toNativeSeparators(cwd).toStdWString();
    if (ok) ok = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | DETACHED_PROCESS | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(), &startup.StartupInfo, &process) != FALSE;
    if (ok) ok = AssignProcessToJobObject(native_->job, process.hProcess) != FALSE;
    if (ok) ok = ResumeThread(process.hThread) != DWORD(-1);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (initialized) DeleteProcThreadAttributeList(attributes);
    close(write); close(input); close(process.hThread);
    native_->process = process.hProcess;
    if (!ok) {
        if (process.hProcess) TerminateProcess(process.hProcess, 1);
        result_.status = ProcessResult::Status::StartFailed;
        result_.detail = QStringLiteral("No se pudo contener/iniciar la preparación (Windows %1).").arg(error);
        // A failed suspended launch cannot write project data.
        QTimer::singleShot(0, this, &TreeProcess::finish);
        return true;
    }
    elapsed_.start(); poll_.start();
    return true;
}
void TreeProcess::permitCompilerTelemetryTail(const QString &path) {
    if (!busy() && QDir::isAbsolutePath(path) && QFileInfo(path).fileName().compare("vctip.exe", Qt::CaseInsensitive) == 0)
        compilerTelemetry_ = QDir::cleanPath(QDir::fromNativeSeparators(path));
}
bool TreeProcess::onlyCompilerTelemetryRemains() const {
    if (compilerTelemetry_.isEmpty()) return false;
    DWORD exit = 1;
    if (!GetExitCodeProcess(native_->process, &exit) || exit != 0) return false;
    std::vector<ULONG_PTR> storage(260, 0);
    auto *list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(storage.data());
    if (!QueryInformationJobObject(native_->job, JobObjectBasicProcessIdList, list, DWORD(storage.size() * sizeof(ULONG_PTR)), nullptr) ||
        list->NumberOfAssignedProcesses != list->NumberOfProcessIdsInList || list->NumberOfProcessIdsInList == 0) return false;
    const auto *ids = reinterpret_cast<const ULONG_PTR *>(reinterpret_cast<const char *>(storage.data()) + offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList));
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
        HANDLE child = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(ids[i]));
        wchar_t path[32768]{}; DWORD size = 32768;
        const bool known = child && QueryFullProcessImageNameW(child, 0, path, &size);
        if (child) CloseHandle(child);
        if (!known || QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(path, int(size)))).compare(compilerTelemetry_, Qt::CaseInsensitive) != 0) return false;
    }
    return true;
}
void TreeProcess::stop(ProcessResult::Status reason) {
    if (stoppingNow_ && result_.status == ProcessResult::Status::Success && reason != ProcessResult::Status::Success) result_.status = reason;
    if (!native_ || stoppingNow_ || committing_) return;
    result_.status = reason;
    stoppingNow_ = true; stopping_.start();
    if (!suppressTermination_) TerminateJobObject(native_->job, 1);
}
void TreeProcess::cancel() { stop(ProcessResult::Status::Cancelled); }
void TreeProcess::tick() {
    if (!native_) return;
    DWORD available = 0;
    // Limit both retained bytes and work per event-loop turn.
    for (int i = 0; i < 16 && PeekNamedPipe(native_->read, nullptr, 0, nullptr, &available, nullptr) && available; ++i) {
        char buffer[4096]; DWORD count = 0;
        if (!ReadFile(native_->read, buffer, qMin<DWORD>(available, sizeof(buffer)), &count, nullptr)) break;
        const qsizetype remaining = limit_ - result_.output.size();
        result_.output.append(buffer, qMin<qsizetype>(remaining, count));
        if (count > remaining) stop(ProcessResult::Status::OutputLimit);
    }
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    const bool known = QueryInformationJobObject(native_->job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) != FALSE;
    const bool rootExited = WaitForSingleObject(native_->process, 0) == WAIT_OBJECT_0;
    if (commitProtocol_ && !committing_ && !stoppingNow_ && !rootExited && known && accounting.ActiveProcesses == 1 &&
        (result_.output.endsWith("ARTESTDEV_COMMIT_READY\r\n") || result_.output.endsWith("ARTESTDEV_COMMIT_READY\n"))) {
        if (elapsed_.elapsed() >= timeoutMs_) stop(ProcessResult::Status::Timeout);
        else if (limit_ - result_.output.size() < qMin<qsizetype>(16384, limit_ / 2)) stop(ProcessResult::Status::OutputLimit);
        else {
            DWORD written = 0;
            if (WriteFile(native_->input, "commit\n", 7, &written, nullptr) && written == 7) {
                committing_ = true; commitTime_.start();
            } else stop(ProcessResult::Status::Failed);
        }
    }
    if (known && accounting.ActiveProcesses == 0 && rootExited) {
        if (PeekNamedPipe(native_->read, nullptr, 0, nullptr, &available, nullptr) && available) return;
        DWORD code = 1; GetExitCodeProcess(native_->process, &code); result_.exitCode = int(code);
        if (!stoppingNow_) result_.status = code == 0 ? ProcessResult::Status::Success : ProcessResult::Status::Failed;
        if (stoppingNow_ && result_.status == ProcessResult::Status::Success) result_.detail += QStringLiteral(" Confirmed: zero Job processes and root exit.");
        if (reported_) { poll_.stop(); native_.reset(); emit settled(); }
        else finish();
        return;
    }
    if (!stoppingNow_ && (rootExited || elapsed_.elapsed() >= timeoutMs_)) {
        if (rootExited && elapsed_.elapsed() < timeoutMs_ && onlyCompilerTelemetryRemains()) {
            result_.detail = QStringLiteral("MSBuild exited successfully. Terminating the exact inventoried MSVC telemetry tail; completion still requires zero Job processes.");
            stop(ProcessResult::Status::Success);
        } else stop(rootExited ? ProcessResult::Status::Failed : ProcessResult::Status::Timeout);
    }
    if (!reported_ && ((stoppingNow_ && stopping_.elapsed() >= 2000) || (committing_ && commitTime_.elapsed() >= 5000))) {
        reported_ = true;
        result_.status = ProcessResult::Status::TerminationUnconfirmed;
        result_.detail = QStringLiteral("Terminación o publicación no confirmada; operación bloqueada. Conserve bloqueos, selección y evidencia. Procesos activos: %1; publicación iniciada: %2.").arg(known ? int(accounting.ActiveProcesses) : -1).arg(committing_);
        emit completed(result_);
    }
}
void TreeProcess::finish() {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    const bool audited = audit_ && native_ && QueryInformationJobObject(native_->job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr);
    poll_.stop(); native_.reset();
    if (audited) emit processAuditFinished(accounting.TotalProcesses);
    emit completed(result_);
}
}

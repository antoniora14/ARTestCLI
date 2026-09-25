#include "ProcessAdapter.h"

#include <QtGlobal>
#include <utility>

namespace ARTestDev {
ProcessAdapter::ProcessAdapter(QObject *parent, KillHandler killHandler, int cleanupMs)
    : QObject(parent), killHandler_(std::move(killHandler)), cleanupMs_(qMax(1, cleanupMs)) {
    timeout_.setSingleShot(true);
    cleanup_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] { stop(ProcessResult::Status::Timeout); });
    connect(&cleanup_, &QTimer::timeout, this, [this] {
        if (state_ != State::Stopping) return;
        if (process_ && process_->state() == QProcess::NotRunning)
            finish(stoppingFor_, QStringLiteral("La salida del proceso se confirmó tras solicitar el cierre."));
        else
            finish(ProcessResult::Status::TerminationUnconfirmed,
                   QStringLiteral("La terminación no se confirmó. No inicie otra comprobación hasta que el proceso salga."));
    });
}

ProcessAdapter::~ProcessAdapter() {
    timeout_.stop();
    cleanup_.stop();
    if (!process_) return;
    process_->disconnect(this);
    if (process_->state() == QProcess::NotRunning) {
        delete process_;
        return;
    }
    // Keep a live owner and finished observer when the window closes before OS exit.
    auto *reaper = new QObject;
    process_->setParent(reaper);
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), reaper,
            [reaper](int, QProcess::ExitStatus) { reaper->deleteLater(); });
    process_->kill();
    process_ = nullptr;
}

bool ProcessAdapter::busy() const { return state_ != State::Idle; }
bool ProcessAdapter::terminationUnconfirmed() const { return state_ == State::Unconfirmed; }

bool ProcessAdapter::start(const QString &program, const QStringList &arguments, int timeoutMs, qsizetype outputLimit) {
    if (busy() || process_ || program.isEmpty() || timeoutMs < 1 || timeoutMs > 60000 ||
        outputLimit < 1 || outputLimit > 1024 * 1024) return false;
    process_ = new QProcess;
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->setStandardInputFile(QProcess::nullDevice());
    output_.clear();
    outputLimit_ = outputLimit;
    state_ = State::Running;
    connect(process_, &QProcess::readyReadStandardOutput, this, &ProcessAdapter::capture);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && state_ == State::Running)
            finish(ProcessResult::Status::StartFailed, process_->errorString());
    });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exit) {
                capture();
                if (state_ == State::Unconfirmed) {
                    releaseProcess();
                    state_ = State::Idle;
                    emit settled();
                } else if (state_ == State::Stopping) {
                    finish(stoppingFor_, QStringLiteral("Proceso terminado tras solicitar el cierre."));
                } else if (state_ == State::Running) {
                    finish(code == 0 && exit == QProcess::NormalExit ? ProcessResult::Status::Success
                                                                      : ProcessResult::Status::Failed,
                           QStringLiteral("Código de salida %1.").arg(code));
                }
            });
    process_->start(program, arguments, QIODevice::ReadOnly);
    if (state_ == State::Running) timeout_.start(timeoutMs);
    return true;
}

void ProcessAdapter::capture() {
    if (!process_ || state_ == State::Idle) return;
    const qsizetype available = qMax<qsizetype>(0, outputLimit_ - output_.size());
    const QByteArray data = process_->read(available + 1);
    if (available > 0) output_.append(data.constData(), qMin(available, data.size()));
    if ((data.size() > available || process_->bytesAvailable() > 0) && state_ == State::Running)
        stop(ProcessResult::Status::OutputLimit);
}

void ProcessAdapter::stop(ProcessResult::Status reason) {
    if (state_ != State::Running || !process_) return;
    stoppingFor_ = reason;
    state_ = State::Stopping;
    timeout_.stop();
    if (process_->state() == QProcess::NotRunning) {
        finish(reason, QStringLiteral("La salida del proceso ya estaba confirmada."));
        return;
    }
    process_->closeReadChannel(QProcess::StandardOutput);
    if (killHandler_) killHandler_(process_);
    else process_->kill();
    cleanup_.start(cleanupMs_);
}

void ProcessAdapter::cancel() { stop(ProcessResult::Status::Cancelled); }

void ProcessAdapter::releaseProcess() {
    if (!process_) return;
    QProcess *finished = process_;
    process_ = nullptr;
    finished->disconnect(this);
    finished->deleteLater();
}

void ProcessAdapter::finish(ProcessResult::Status status, const QString &detail) {
    if (state_ == State::Idle || state_ == State::Unconfirmed) return;
    timeout_.stop();
    cleanup_.stop();
    ProcessResult result;
    result.status = status;
    result.exitCode = process_ && process_->state() == QProcess::NotRunning ? process_->exitCode() : -1;
    result.output = output_;
    result.detail = detail;
    if (status == ProcessResult::Status::TerminationUnconfirmed) state_ = State::Unconfirmed;
    else {
        state_ = State::Idle;
        releaseProcess();
    }
    emit completed(result);
}
}

#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>

namespace ARTestDev {
struct ProcessResult {
    enum class Status { Success, Failed, StartFailed, Cancelled, Timeout, OutputLimit, TerminationUnconfirmed };
    Status status = Status::StartFailed;
    int exitCode = -1;
    QByteArray output;
    QString detail;
};

class ProcessAdapter final : public QObject {
    Q_OBJECT
public:
    using KillHandler = std::function<void(QProcess *)>;
    explicit ProcessAdapter(QObject *parent = nullptr, KillHandler killHandler = {}, int cleanupMs = 2000);
    ~ProcessAdapter() override;
    bool start(const QString &program, const QStringList &arguments, int timeoutMs = 15000, qsizetype outputLimit = 256 * 1024);
    void cancel();
    bool busy() const;
    bool terminationUnconfirmed() const;
signals:
    void completed(const ARTestDev::ProcessResult &result);
    void settled();
private:
    enum class State { Idle, Running, Stopping, Unconfirmed };
    void capture();
    void stop(ProcessResult::Status reason);
    void finish(ProcessResult::Status status, const QString &detail = {});
    void releaseProcess();
    QProcess *process_ = nullptr;
    QTimer timeout_;
    QTimer cleanup_;
    QByteArray output_;
    qsizetype outputLimit_ = 0;
    ProcessResult::Status stoppingFor_ = ProcessResult::Status::Timeout;
    KillHandler killHandler_;
    int cleanupMs_ = 2000;
    State state_ = State::Idle;
};
}

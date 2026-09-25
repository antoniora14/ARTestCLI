#pragma once
#include "ProcessAdapter.h"
#include <QElapsedTimer>
#include <memory>

namespace ARTestDev {
// Preparation children must enter containment before executing any user code.
class TreeProcess final : public QObject {
    Q_OBJECT
public:
    explicit TreeProcess(QObject *parent = nullptr);
    ~TreeProcess() override;
    bool start(const QString &program, const QStringList &arguments, const QString &cwd,
               int timeoutMs = 300000, qsizetype outputLimit = 256 * 1024, bool commitProtocol = false);
    bool busy() const;
    void cancel();
    void enableProcessAudit() { audit_ = true; }
    void permitCompilerTelemetryTail(const QString &path);
#ifdef ARTESTDEV_TESTING
    void suppressTerminationForTest() { suppressTermination_ = true; }
#endif
signals:
    void processObserved(quint64 id, const QString &image);
    void processAuditFinished(quint64 totalProcesses);
    void completed(const ARTestDev::ProcessResult &result);
    void settled();
private:
    struct Native;
    std::unique_ptr<Native> native_;
    QTimer poll_;
    QElapsedTimer elapsed_, stopping_;
    ProcessResult result_;
    int timeoutMs_ = 0;
    qsizetype limit_ = 0;
    bool stoppingNow_ = false, reported_ = false;
    bool commitProtocol_ = false, committing_ = false;
    QElapsedTimer commitTime_;
    bool suppressTermination_ = false;
    bool audit_ = false;
    QString compilerTelemetry_;
    bool onlyCompilerTelemetryRemains() const;
    void tick();
    void stop(ProcessResult::Status reason);
    void finish();
};
}

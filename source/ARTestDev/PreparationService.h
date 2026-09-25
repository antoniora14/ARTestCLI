#pragma once
#include "TreeProcess.h"
#include <QJsonObject>
#include <QFutureWatcher>
#include "Inspection.h"

namespace ARTestDev {
struct PreparationResult {
    enum class Status { Prepared, Reused, Failed, Cancelled, Timeout, TerminationUnconfirmed };
    Status status = Status::Failed;
    QString identity, revision, package, receipt, association, diagnostic;
    QByteArray output;
};
class PreparationService final : public QObject {
    Q_OBJECT
public:
    explicit PreparationService(QObject *parent = nullptr);
#ifdef ARTESTDEV_TESTING
    PreparationService(const QString &stagedExecutable, QObject *parent = nullptr) : PreparationService(parent) { executable_ = stagedExecutable; }
#endif
    // The caller supplies the current local Python selection; preparation probes it again.
    bool start(const QString &projectRoot, const QString &selectedPython, int timeoutMs = 300000);
    bool busy() const;
    void cancel();
signals:
    void completed(const ARTestDev::PreparationResult &result);
    void settled();
private:
    TreeProcess process_;
    QString executable_;
    QFutureWatcher<Kit> inspection_;
    bool checking_ = false, cancelled_ = false;
};
}

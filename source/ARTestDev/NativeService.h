#pragma once
#include "TreeProcess.h"
#include <QJsonObject>
#include <QFutureWatcher>
#include "Inspection.h"
namespace ARTestDev {
class NativeService final : public QObject {
    Q_OBJECT
public:
    explicit NativeService(QObject *parent = nullptr);
    bool start(const QString &project, const QString &configuration, const QString &cli = {}, int timeoutMs = 300000);
    bool busy() const { return checking_ || process_.busy(); }
    void cancel() { cancelled_ = true; process_.cancel(); }
#ifdef ARTESTDEV_TESTING
    void setExecutable(const QString &path) { executable_ = path; }
#endif
signals:
    void completed(const QJsonObject &result, const ARTestDev::ProcessResult &process);
    void settled();
private:
    TreeProcess process_;
    QString executable_;
    QFutureWatcher<Kit> inspection_;
    bool checking_ = false, cancelled_ = false;
};
}

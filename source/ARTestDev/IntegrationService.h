#pragma once
#include "Inspection.h"
#include <QFutureWatcher>
#include <QJsonObject>
#include <atomic>
#include <memory>

namespace ARTestDev {
struct IntegrationRequest {
    Project project;
    QString python, configuration = "Release", registryRoot, stagedExecutable;
    QJsonObject target;
};
struct IntegrationResult {
    bool success = false;
    QString target, revision, diagnostic;
};
class IntegrationService final : public QObject {
    Q_OBJECT
public:
    explicit IntegrationService(QObject *parent = nullptr);
    ~IntegrationService() override;
    bool start(const IntegrationRequest &request);
    bool busy() const { return running_; }
    void cancel();
signals:
    void progress(const QString &message);
    void completed(const ARTestDev::IntegrationResult &result);
private:
    QFutureWatcher<IntegrationResult> worker_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    bool running_ = false;
};
}

#pragma once
#include "IntegrationService.h"
#include "ProcessAdapter.h"
#include "TreeProcess.h"
#include <functional>

namespace ARTestDev {
struct TestPlanRequest : IntegrationRequest {
    QString plan;
    bool sources = false;
    int executionTimeoutMs = 300000;
};
struct TestPlanResult {
    bool validated = false, executed = false;
    QString revision, evidence, diagnostic;
    ProcessResult process;
    QJsonObject report;
};
// A validated operation retains its inputs and target lease until Execute/Cancel.
class TestPlanService final : public QObject {
    Q_OBJECT
public:
    using ProcessFactory = std::function<std::unique_ptr<TreeProcess>()>;
    explicit TestPlanService(QObject *parent = nullptr, ProcessFactory factory = {});
    ~TestPlanService() override;
    bool start(const TestPlanRequest &request);
    bool busy() const { return running_; }
    bool ready() const { return ready_; }
    void execute();
    void cancel();
signals:
    void progress(const QString &message);
    void validated(const ARTestDev::TestPlanResult &result);
    void completed(const ARTestDev::TestPlanResult &result);
private:
    QFutureWatcher<TestPlanResult> worker_;
    std::shared_ptr<std::atomic_int> decision_;
    bool running_ = false, ready_ = false;
    ProcessFactory factory_;
};
}

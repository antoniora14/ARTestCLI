#include "NativeService.h"
#include "SdkLocation.h"
#include <QtConcurrentRun>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
namespace ARTestDev {
NativeService::NativeService(QObject *parent) : QObject(parent) {
    connect(&process_, &TreeProcess::settled, this, &NativeService::settled);
    connect(&process_, &TreeProcess::completed, this, [this](const ProcessResult &p) {
        auto report = QJsonDocument::fromJson(p.output).object();
        if (p.status != ProcessResult::Status::Success && (report.value("status") == "compiled" || report.value("status") == "target-validated"))
            report = {{"status", "failed"}, {"diagnostic", "Child process did not settle successfully"}};
        emit completed(report, p);
    });
}
bool NativeService::start(const QString &project, const QString &configuration, const QString &cli, int timeoutMs) {
    if (busy() || !QDir::isAbsolutePath(project) || (configuration != "Debug" && configuration != "Release")) return false;
    const QString helper = executable_.isEmpty() ? QCoreApplication::applicationDirPath() + "/ARTestDevNative.exe" : executable_;
    QStringList args{cli.isEmpty() ? "check-project" : "validate-project", project, configuration, QFileInfo(helper).absolutePath() + "/native-sdk"};
    if (!cli.isEmpty()) args << cli;
    checking_ = true; cancelled_ = false;
    connect(&inspection_, &QFutureWatcher<Kit>::finished, this, [this, helper, args, project, timeoutMs] {
        checking_ = false;
        const auto kit = inspection_.result();
        if (!cancelled_ && kit.valid && process_.start(helper, args, QFileInfo(project).absolutePath(), timeoutMs, 1024 * 1024)) return;
        ProcessResult p; p.status = cancelled_ ? ProcessResult::Status::Cancelled : ProcessResult::Status::Failed;
        p.detail = kit.diagnostics.join('\n');
        emit completed({{"status", "failed"}, {"diagnostic", p.detail}}, p);
    }, Qt::SingleShotConnection);
    inspection_.setFuture(QtConcurrent::run([helper] { return inspectStagingExecutable(QFileInfo(helper).absolutePath() + "/ARTestDev.exe"); }));
    return true;
}
}

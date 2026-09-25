#include "PreparationService.h"
#include "SdkLocation.h"
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <QtConcurrentRun>
#include <QCoreApplication>

namespace ARTestDev {
PreparationService::PreparationService(QObject *parent) : QObject(parent) {
    connect(&process_, &TreeProcess::settled, this, &PreparationService::settled);
    connect(&process_, &TreeProcess::completed, this, [this](const ProcessResult &process) {
        PreparationResult result;
        result.output = process.output;
        result.diagnostic = process.detail;
        const QByteArray marker = "ARTESTDEV_PREPARATION=";
        const qsizetype index = process.output.lastIndexOf(marker);
        const auto value = index < 0 ? QJsonObject{} : QJsonDocument::fromJson(process.output.mid(index + marker.size()).trimmed()).object();
        if (process.status == ProcessResult::Status::Success && value.value("success").toBool()) {
            result.identity = value.value("preparationId").toString();
            result.revision = value.value("revision").toString();
            result.package = value.value("package").toString();
            result.receipt = value.value("receipt").toString();
            result.association = value.value("association").toString();
            if (result.identity.size() == 64 && !result.receipt.isEmpty()) {
                result.status = value.value("reused").toBool() ? PreparationResult::Status::Reused : PreparationResult::Status::Prepared;
                result.diagnostic = value.value("reused").toBool() ? QStringLiteral("Preparación verificada y reutilizada.") : QStringLiteral("Preparación completada en su ubicación definitiva.");
            }
        } else if (process.status == ProcessResult::Status::Cancelled) result.status = PreparationResult::Status::Cancelled;
        else if (process.status == ProcessResult::Status::Timeout) result.status = PreparationResult::Status::Timeout;
        else if (process.status == ProcessResult::Status::TerminationUnconfirmed) result.status = PreparationResult::Status::TerminationUnconfirmed;
        if (!value.value("diagnostic").toString().isEmpty()) result.diagnostic += '\n' + value.value("diagnostic").toString();
        if (result.status != PreparationResult::Status::Prepared && result.status != PreparationResult::Status::Reused)
            result.diagnostic += QStringLiteral("\nPreparación fallida/interrumpida. Conserve la selección anterior y la evidencia; inspeccione cualquier preparation.lock o work pendiente antes de reintentar. No se registró ni ejecutó un Test plan.");
        emit completed(result);
    });
}
bool PreparationService::busy() const { return checking_ || process_.busy(); }
void PreparationService::cancel() { cancelled_ = true; process_.cancel(); }
bool PreparationService::start(const QString &projectRoot, const QString &selectedPython, int timeoutMs) {
    if (busy()) return false;
    checking_ = true; cancelled_ = false;
    const QString executable = executable_.isEmpty() ? QCoreApplication::applicationFilePath() : executable_;
    connect(&inspection_, &QFutureWatcher<Kit>::finished, this, [this, projectRoot, selectedPython, timeoutMs] {
        const Kit sdk = inspection_.result();
        checking_ = false;
        PreparationResult result;
        if (cancelled_) {
            result.status = PreparationResult::Status::Cancelled;
            result.diagnostic = QStringLiteral("Cancelado antes de iniciar la preparación.");
        } else if (sdk.valid && QDir::isAbsolutePath(projectRoot) &&
                   process_.start(selectedPython, {"-I", "-B", sdk.root + "/python/tools/prepare.py",
                        "--project", projectRoot, "--python", selectedPython}, projectRoot, timeoutMs, 256 * 1024, true)) return;
        else result.diagnostic = sdk.diagnostics.join('\n') + QStringLiteral("\nSeleccione un proyecto externo al SDK y CPython 3.13 x64 con GIL instalado; compruebe el tiempo límite.");
        emit completed(result);
    }, Qt::SingleShotConnection);
    inspection_.setFuture(QtConcurrent::run([executable] { return inspectStagingExecutable(executable); }));
    return true;
}
}

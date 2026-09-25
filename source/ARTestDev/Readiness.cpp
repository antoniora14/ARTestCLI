#include "Readiness.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace ARTestDev {
CheckResult parsePythonCheck(const ProcessResult &process) {
    CheckResult out;
    out.status = CheckStatus::Failed;
    if (process.status == ProcessResult::Status::TerminationUnconfirmed) {
        out.diagnostics << QStringLiteral("Terminación de Python no confirmada; espere la salida observada antes de volver a comprobar.");
        return out;
    }
    if (process.status == ProcessResult::Status::Cancelled) {
        out.status = CheckStatus::Blocked;
        out.diagnostics << QStringLiteral("Comprobación cancelada; no se considera correcta.");
        return out;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(process.output, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        out.diagnostics << QStringLiteral("Python check no devolvió un informe JSON válido. %1").arg(process.detail);
        return out;
    }
    const QJsonObject report = document.object();
    if (report.value(QStringLiteral("operation")).toString() != QStringLiteral("check") ||
        !report.value(QStringLiteral("success")).isBool() || !report.value(QStringLiteral("diagnostics")).isArray()) {
        out.diagnostics << QStringLiteral("Informe Python check incompatible; no se declara preparación completa.");
        return out;
    }
    const bool success = report.value(QStringLiteral("success")).toBool();
    bool missing = false;
    for (const QJsonValue &entry : report.value(QStringLiteral("diagnostics")).toArray()) {
        if (!entry.isObject()) { out.diagnostics << QStringLiteral("Diagnóstico Python malformado."); continue; }
        const QJsonObject item = entry.toObject();
        const QString code = item.value(QStringLiteral("code")).toString();
        const QString field = item.value(QStringLiteral("field")).toString();
        const QString cause = item.value(QStringLiteral("cause")).toString();
        const QString correction = item.value(QStringLiteral("correction")).toString();
        const QString line = QStringLiteral("%1 (%2): %3. Corrección: %4").arg(code, field, cause, correction);
        if (code.contains(QStringLiteral("MISSING")) || code.contains(QStringLiteral("INCOMPATIBLE"))) missing = true;
        if (code.startsWith(QStringLiteral("CLI_"))) out.targetDiagnostics << line;
        else out.diagnostics << line;
    }
    if (success && process.status == ProcessResult::Status::Success && process.exitCode == 0 &&
        out.diagnostics.isEmpty() && out.targetDiagnostics.isEmpty()) {
        out.status = CheckStatus::Ready;
    } else if (!success && process.status == ProcessResult::Status::Failed && missing) {
        out.status = CheckStatus::Missing;
    } else {
        out.status = CheckStatus::Failed;
        if (out.diagnostics.isEmpty() && out.targetDiagnostics.isEmpty())
            out.diagnostics << QStringLiteral("Python check falló o informó un resultado incompatible. %1").arg(process.detail);
    }
    return out;
}

void CheckTracker::selectionChanged() {
    ++serial_;
    active_ = 0;
    result_ = {};
}

quint64 CheckTracker::begin() {
    active_ = ++serial_;
    result_ = {CheckStatus::Pending, {}, {}};
    return active_;
}

bool CheckTracker::accept(quint64 token, const ProcessResult &process) {
    if (token == 0 || token != active_ || result_.status != CheckStatus::Pending) return false;
    active_ = 0;
    result_ = parsePythonCheck(process);
    return true;
}

void CheckTracker::set(CheckStatus status, const QStringList &diagnostics) {
    ++serial_;
    active_ = 0;
    result_ = {status, diagnostics, {}};
}
}

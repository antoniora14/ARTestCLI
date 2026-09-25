#pragma once

#include "ProcessAdapter.h"

#include <QStringList>

namespace ARTestDev {
enum class CheckStatus { NotRequired, Pending, Ready, Missing, Failed, Blocked };

struct CheckResult {
    CheckStatus status = CheckStatus::NotRequired;
    QStringList diagnostics;
    QStringList targetDiagnostics;
};

CheckResult parsePythonCheck(const ProcessResult &process);

class CheckTracker {
public:
    void selectionChanged();
    quint64 begin();
    bool accept(quint64 token, const ProcessResult &process);
    void set(CheckStatus status, const QStringList &diagnostics = {});
    const CheckResult &result() const { return result_; }
private:
    quint64 serial_ = 0;
    quint64 active_ = 0;
    CheckResult result_;
};
}

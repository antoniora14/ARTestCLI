#pragma once
#include <QJsonArray>
#include <QStringList>

namespace ARTestDev {
struct Presentation {
    QJsonArray components;
    QStringList diagnostics;
    bool current = false;
};
QString comparisonName(const QString &name);
QString nameSimilarity(const QString &left, const QString &right);
Presentation inspectPresentation(const QString &project);
QStringList localNameWarnings(const QString &workspace, const QString &self, const QJsonArray &components, const QString &extensionId = {});
}

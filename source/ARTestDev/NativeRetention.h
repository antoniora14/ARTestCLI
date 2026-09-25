#pragma once
#include <QJsonObject>
#include <QString>
namespace ARTestDev::Native {
// Call only under the configuration lock, after child settlement and publication recovery.
void sealScratch(const QString &path, const QString &project);
QJsonObject verifyScratch(const QString &path, const QString &project);
void retireScratch(const QString &state, const QString &path, const QString &project);
void resumeRetirement(const QString &state, const QString &project);
}

#pragma once
#include <QJsonObject>
#include <QString>

namespace ARTestDev::Native {
// Private tooling state. No runtime manifest, receipt or ABI is defined here.
void require(bool condition, const QString &message);
QString absolute(const QString &path);
void ordinary(const QString &path);
QString digest(const QString &path);
QJsonObject readObject(const QString &path);
void writeObject(const QString &path, const QJsonObject &value);
QJsonObject inventory(const QString &root, bool sources = false);
QJsonObject inputs(const QJsonObject &request);
QJsonObject inspect(const QJsonObject &request);
QJsonObject build(const QJsonObject &request, bool rebuild = false);
QJsonObject clean(const QJsonObject &request);
QJsonObject validate(const QJsonObject &request, const QString &cli);
void recover(const QString &state, const QString &project);
#ifdef ARTESTDEV_TESTING
void publishFixture(const QString &state, const QString &project, const QString &revision);
#endif
}

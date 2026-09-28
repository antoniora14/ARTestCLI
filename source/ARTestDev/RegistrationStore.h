#pragma once
#include <QJsonObject>
#include <QStringList>
#include <memory>

namespace ARTestDev::Registration {
void require(bool ok, const QString &message);
QString absolute(const QString &path);
void ordinary(const QString &path);
QByteArray read(const QString &path);
QJsonObject object(const QByteArray &bytes);
QJsonObject load(const QString &path);
void write(const QString &path, const QByteArray &bytes);
void save(const QString &path, const QJsonObject &value);
QString hash(const QByteArray &bytes);
QString digest(const QString &path);
QJsonObject inventory(const QString &path);
void copy(const QString &from, const QString &to);
void move(const QString &from, const QString &to);
QString configurationRoot();
QJsonObject registry(const QString &root);
QJsonObject manualProfile(const QString &path, const QString &root);
QList<QJsonObject> candidates(const QString &root);
void checkProfile(const QJsonObject &profile);
class Lock final {
public:
    explicit Lock(const QString &path);
    ~Lock();
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
private:
    void *handle_;
};
// Legacy writer locks and profile/state formats, with a private restartable
// rollback cursor. Retired trees are retained; unknown files are never deleted.
class Transaction final {
public:
    Transaction(QJsonObject profile, QString root);
    void recover();
    void stage(const QString &package, const QString &receipt, const QString &revision,
               const QString &extension, const QString &language);
    QString candidate() const;
    QString mapping() const;
    void promote();
    QString commit();
private:
    QJsonObject profile_, journal_;
    QString root_, journalPath_;
    std::unique_ptr<Lock> selectionLock_, targetLock_;
    void phase(const QString &value);
    void validateJournal(const QJsonObject &journal, bool recovering) const;
};
}

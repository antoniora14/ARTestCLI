#include "NativeRetention.h"
#include "NativeOutputs.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QScopeGuard>
#include <algorithm>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ARTestDev::Native {
namespace {
QString directoryIdentity(const QString &path) {
    ordinary(path);
    HANDLE directory = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(directory != INVALID_HANDLE_VALUE, "Cannot identify retirement directory");
    const auto close = qScopeGuard([&] { CloseHandle(directory); });
    BY_HANDLE_FILE_INFORMATION info{};
    require(GetFileInformationByHandle(directory, &info) && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "Unsafe retirement directory");
    return QString::number(info.dwVolumeSerialNumber, 16) + ':' + QString::number(info.nFileIndexHigh, 16) + ':' + QString::number(info.nFileIndexLow, 16);
}
void deleteVerified(const QString &path, const QString &hash) {
    HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(file != INVALID_HANDLE_VALUE, "Retirement file busy/inaccessible; retry: " + path);
    const auto close = qScopeGuard([&] { CloseHandle(file); });
    BY_HANDLE_FILE_INFORMATION info{};
    require(GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)), "Unsafe retirement file");
    QCryptographicHash digest(QCryptographicHash::Sha256); QByteArray buffer(1024 * 1024, Qt::Uninitialized); DWORD count = 0;
    for (;;) {
        require(ReadFile(file, buffer.data(), DWORD(buffer.size()), &count, nullptr), "Cannot verify retirement file");
        if (!count) break;
        digest.addData(QByteArrayView(buffer.constData(), count));
    }
    require(QString::fromLatin1(digest.result().toHex()) == hash, "Changed retirement file preserved: " + path);
    FILE_DISPOSITION_INFO disposition{TRUE};
    require(SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition)), "Cannot retire file; retry: " + path);
}
}
void sealScratch(const QString &path, const QString &project) {
    auto files = inventory(path); files.remove("scratch.json");
    writeObject(path + "/scratch.json", {{"format", "ARTestDev.Scratch.1"}, {"project", project}, {"files", files}});
}
QJsonObject verifyScratch(const QString &path, const QString &project) {
    const auto owner = readObject(path + "/scratch.json");
    require(owner.value("format") == "ARTestDev.Scratch.1" && owner.value("project") == project && owner.value("files").isObject(), "Unknown scratch ownership preserved: " + path);
    const auto actual = inventory(path); auto payload = actual; payload.remove("scratch.json");
    const auto marker = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(owner).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    require(payload == owner.value("files").toObject() && actual.value("scratch.json") == marker, "Changed/unknown scratch files preserved: " + path);
    return actual;
}
void resumeRetirement(const QString &state, const QString &project) {
    const QString journal = state + "/retirement.json";
    ordinary(journal);
    if (!QFileInfo::exists(journal)) return;
    require(!QFileInfo::exists(state + "/transaction.json"), "Publication recovery precedes retirement");
    const auto record = readObject(journal);
    const QString name = record.value("name").toString();
    require(record.value("format") == "ARTestDev.Retirement.1" && record.value("project") == project && record.value("state") == state &&
        record.size() == 6 && record.value("files").isObject() &&
        QRegularExpression("^[0-9a-f]+(:[0-9a-f]+){2}$").match(record.value("directoryIdentity").toString()).hasMatch() &&
        (name == "intermediates" || QRegularExpression("^(work|evaluation|validation)-[0-9a-f]{32}$").match(name).hasMatch()), "Unknown retirement intent preserved");
    const auto expected = record.value("files").toObject();
    require(QRegularExpression("^[0-9a-f]{64}$").match(expected.value("scratch.json").toString()).hasMatch(), "Missing retirement ownership proof");
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        QString relative = it.key(); if (relative.endsWith('/')) relative.chop(1);
        require(!relative.isEmpty() && !QDir::isAbsolutePath(relative) && !relative.contains('\\') && !relative.contains(':') &&
            !relative.split('/').contains("..") && !relative.split('/').contains(".") && !relative.split('/').contains("") &&
            (it.key().endsWith('/') ? it.value() == "directory" : QRegularExpression("^[0-9a-f]{64}$").match(it.value().toString()).hasMatch()), "Invalid retirement inventory preserved");
    }
    const QString path = state + '/' + name;
    ordinary(path);
    if (QFileInfo::exists(path)) {
        require(record.value("directoryIdentity").toString() == directoryIdentity(path), "Foreign retirement directory identity preserved");
        const auto actual = inventory(path);
        // Only retirement accepts missing entries. Package validation always requires the full inventory.
        for (auto it = actual.begin(); it != actual.end(); ++it)
            require(expected.contains(it.key()) && expected.value(it.key()) == it.value(), "Unknown/changed retirement remainder preserved: " + path + '/' + it.key());
        auto names = actual.keys();
        std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
        for (const auto &relative : names) {
            const QString entry = path + '/' + relative;
            ordinary(entry);
            const bool directory = actual.value(relative) == "directory";
            if (directory) require(QDir().rmdir(entry), "Retirement incomplete; preserve intent and retry: " + entry);
            else deleteVerified(entry, actual.value(relative).toString());
#ifdef ARTESTDEV_TESTING
            if (qEnvironmentVariable("ARTESTDEV_FAULT_PHASE") == "retirement-delete") {
                QFile marker(qEnvironmentVariable("ARTESTDEV_FAULT_MARKER"));
                require(marker.open(QIODevice::WriteOnly), "Fault marker"); marker.write("retirement-delete"); marker.close(); Sleep(INFINITE);
            }
#endif
        }
        require(QDir().rmdir(path), "Cannot retire empty scratch root: " + path);
    }
    require(QFile::remove(journal), "Cannot retire cleanup intent");
}
void retireScratch(const QString &state, const QString &path, const QString &project) {
    require(QFileInfo(path).absolutePath() == state && !QFileInfo::exists(state + "/transaction.json"), "Unsafe scratch retirement");
    resumeRetirement(state, project);
    const auto directory = directoryIdentity(path);
    const auto files = verifyScratch(path, project);
    require(directory == directoryIdentity(path), "Retirement directory replaced during verification");
    writeObject(state + "/retirement.json", {{"format", "ARTestDev.Retirement.1"}, {"project", project}, {"state", state},
        {"name", QFileInfo(path).fileName()}, {"directoryIdentity", directory}, {"files", files}});
    resumeRetirement(state, project);
}
}

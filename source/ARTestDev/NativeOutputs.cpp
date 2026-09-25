#include "NativeOutputs.h"
#include "NativeRetention.h"
#include "SdkLocation.h"
#include "TreeProcess.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QMap>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QUuid>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

namespace ARTestDev::Native {
namespace {
qint64 hashedFiles = 0, hashedBytes = 0;
struct ShaProvider {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ShaProvider() { require(BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "Cannot open Windows SHA-256 provider"); }
    ~ShaProvider() { BCryptCloseAlgorithmProvider(handle, 0); }
};
class ContentHash {
    BCRYPT_HASH_HANDLE handle_ = nullptr;
public:
    ContentHash() {
        static const ShaProvider provider;
        require(BCryptCreateHash(provider.handle, &handle_, nullptr, 0, nullptr, 0, 0) >= 0, "Cannot create SHA-256 hash");
    }
    ~ContentHash() { BCryptDestroyHash(handle_); }
    void add(const QByteArray &buffer, DWORD length) {
        require(BCryptHashData(handle_, reinterpret_cast<PUCHAR>(const_cast<char *>(buffer.constData())), length, 0) >= 0, "Cannot hash content");
    }
    QString finish() {
        QByteArray bytes(32, Qt::Uninitialized);
        require(BCryptFinishHash(handle_, reinterpret_cast<PUCHAR>(bytes.data()), ULONG(bytes.size()), 0) >= 0, "Cannot finish SHA-256 hash");
        return QString::fromLatin1(bytes.toHex());
    }
};
struct InputSnapshot {
    QMap<QString, QPair<HANDLE, QString>> files;
    QJsonObject projectFiles;
    bool kitChecked = false;
    ~InputSnapshot() { for (auto it = files.begin(); it != files.end(); ++it) CloseHandle(it.value().first); }
};
InputSnapshot *snapshot = nullptr;
bool inputHashing = false;
QString hashFile(const QString &path) {
    if (snapshot && inputHashing) {
        const auto found = snapshot->files.constFind(path);
        if (found != snapshot->files.cend()) return found.value().second;
        // A cache hit is valid only while this handle denies writes and replacement.
        HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(file != INVALID_HANDLE_VALUE, "Input busy or inaccessible; retry after editing: " + path);
        const auto close = qScopeGuard([&] { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); });
        LARGE_INTEGER size{};
        require(GetFileSizeEx(file, &size) && size.QuadPart <= 512LL * 1024 * 1024, "Oversized input: " + path);
        ContentHash hash;
        QByteArray buffer(1024 * 1024, Qt::Uninitialized); DWORD read = 0;
        for (;;) {
            require(ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr), "Cannot hash locked input: " + path);
            if (!read) break;
            hash.add(buffer, read);
        }
        const QString value = hash.finish();
        ++hashedFiles; hashedBytes += size.QuadPart;
        snapshot->files.insert(path, {file, value}); file = INVALID_HANDLE_VALUE;
        return value;
    }
    QFile f(path); QCryptographicHash h(QCryptographicHash::Sha256);
    require(f.open(QIODevice::ReadOnly) && f.size() <= 512LL * 1024 * 1024 && h.addData(&f), "Cannot hash input (maximum 512 MiB per file): " + path);
    ++hashedFiles; hashedBytes += f.size();
    return QString::fromLatin1(h.result().toHex());
}
}
void require(bool ok, const QString &message) { if (!ok) throw std::runtime_error(message.toUtf8().constData()); }
QString absolute(const QString &path) { return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath())); }
void ordinary(const QString &path) {
    require(QDir::isAbsolutePath(path) && !path.startsWith("//"), "Absolute local path required: " + path);
    for (QString p = absolute(path);;) {
        const DWORD a = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(p).utf16()));
        require(a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_REPARSE_POINT), "Reparse point rejected: " + p);
        const QString parent = QFileInfo(p).absolutePath();
        if (parent == p) break;
        p = parent;
    }
}
QString digest(const QString &path) {
    ordinary(path); return hashFile(path);
}
QJsonObject readObject(const QString &path) {
    ordinary(path); QFile f(path);
    require(f.open(QIODevice::ReadOnly) && f.size() <= 32 * 1024 * 1024, "Cannot read private state: " + path);
    const auto bytes = f.readAll();
    QJsonParseError e; const auto d = QJsonDocument::fromJson(bytes, &e);
    require(e.error == QJsonParseError::NoError && d.isObject(), "Invalid private state: " + path);
    if (QStringList{"transaction.json", "ownership.json", "inputs.json", "scratch.json", "retirement.json"}.contains(QFileInfo(path).fileName()))
        require(bytes == d.toJson(QJsonDocument::Compact), "Ambiguous/noncanonical private state preserved: " + path);
    return d.object();
}
void writeObject(const QString &path, const QJsonObject &value) {
    ordinary(path); QSaveFile f(path); const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit(), "Cannot commit private state: " + path);
}
namespace {
void mkdir(const QString &p) { ordinary(p); require(QDir().mkpath(p), "Cannot create: " + p); }
bool exists(const QString &p) { ordinary(p); return QFileInfo::exists(p); }
QString identity(const QJsonObject &o) { return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(o).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()); }
void scan(const QString &root, const QString &dir, QJsonObject &out, bool sources) {
    ordinary(dir); require(QFileInfo(dir).isDir() && QDir(dir).isReadable(), "Unreadable directory: " + dir);
    for (const auto &f : QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name)) {
        const QString rel = QDir(root).relativeFilePath(f.absoluteFilePath());
        if (sources && dir == root && QStringList{"bin", "obj", "out", ".artest", ".vs", ".git"}.contains(f.fileName(), Qt::CaseInsensitive)) continue;
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(f.absoluteFilePath()).utf16()));
        require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "Reparse/inaccessible entry: " + f.absoluteFilePath());
        if (f.isDir()) { out.insert(rel + '/', "directory"); scan(root, f.absoluteFilePath(), out, sources); }
        else { require(f.isFile() && out.size() < 100000, "Unsupported/oversized tree: " + root); out.insert(rel, hashFile(f.absoluteFilePath())); }
    }
}
class Lock {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
public:
    explicit Lock(const QString &path) {
        ordinary(path);
        handle_ = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(handle_ != INVALID_HANDLE_VALUE, "Operation already active or inaccessible lock: " + path);
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle_, &size) || size.QuadPart != 0) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; require(false, "Unknown lock content preserved: " + path); }
    }
    ~Lock() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
};
QByteArray run(const QString &exe, const QStringList &args, const QString &cwd, const QString &log, const QString &compilerTelemetry = {}) {
    TreeProcess process; QEventLoop loop; ProcessResult result; bool unconfirmed = false;
    if (!compilerTelemetry.isEmpty()) process.permitCompilerTelemetryTail(compilerTelemetry);
    QObject::connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &r) {
        result = r;
        if (r.status == ProcessResult::Status::TerminationUnconfirmed) {
            unconfirmed = true;
            // Keep the project lock and process supervisor alive until actual settlement.
            QFile f(log); if (f.open(QIODevice::WriteOnly)) f.write(r.output + "\nTERMINATION UNCONFIRMED; operation remains locked.\n");
        } else loop.quit();
    });
    QObject::connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
    require(process.start(exe, args, cwd, 300000, 1024 * 1024), "Cannot start bounded native operation: " + exe);
    loop.exec();
    QFile f(log); const auto evidence = result.output + (result.detail.isEmpty() ? QByteArray{} : "\n[supervisor] " + result.detail.toUtf8() + "\n");
    require(f.open(QIODevice::WriteOnly) && f.write(evidence) == evidence.size(), "Cannot retain child output: " + log);
    require(!unconfirmed && result.status == ProcessResult::Status::Success && result.exitCode == 0,
            "Native child failed; see " + log + ": " + result.detail);
    return result.output;
}
QString stateRoot(const QJsonObject &r) {
    const QString project = absolute(r.value("project").toString());
    require(r.value("configuration") == "Debug" || r.value("configuration") == "Release", "Unsupported configuration");
    require(QFileInfo(project).suffix() == "vcxproj", "Expected generated C++ project");
    return QFileInfo(project).absolutePath() + "/.artest/native/" + r.value("configuration").toString();
}
void checkState(const QString &state) {
    ordinary(state);
    for (const auto &entry : QDir(state).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        ordinary(entry.absoluteFilePath());
        const QString name = entry.fileName();
        const bool file = QStringList{"operation.lock", "transaction.json", "retirement.json", "last-success.json", "last-failure.json"}.contains(name);
        const bool directory = QStringList{"current", "candidate", "backup", "intermediates"}.contains(name) ||
            QRegularExpression("^(work|validation|evaluation)-[0-9a-f]{32}$").match(name).hasMatch();
        require((file && entry.isFile()) || (directory && entry.isDir()), "Unknown native state preserved: " + entry.absoluteFilePath());
    }
}
QJsonObject owned(const QString &path, const QString &project) {
    const auto o = readObject(path + "/ownership.json");
    require(o.value("format") == "ARTestDev.NativeOutput.1" && o.value("project") == project && o.value("files").isObject(), "Unknown output ownership: " + path);
    auto actual = inventory(path); actual.remove("ownership.json");
    require(actual == o.value("files").toObject(), "Changed/unknown generated files: " + path);
    return o;
}
void move(const QString &from, const QString &to) { ordinary(from); ordinary(to); require(!exists(to) && QDir().rename(from, to), "Cannot rename " + from + " to " + to); }
void prune(const QString &state, const QString &project) {
    for (const QString &name : QStringList{"last-success.json", "last-failure.json"}) {
        if (!exists(state + '/' + name)) continue;
        const auto record = readObject(state + '/' + name);
        require(record.value("format") == "ARTestDev.NativeDiagnostic.1" && record.value("project") == project && record.value("result").isObject() && record.value("output").isObject(), "Unknown diagnostic preserved: " + name);
    }
    resumeRetirement(state, project);
    for (const auto &entry : QDir(state).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!QRegularExpression("^(work|evaluation|validation)-[0-9a-f]{32}$").match(entry.fileName()).hasMatch()) continue;
        require(exists(entry.absoluteFilePath() + "/scratch.json"), "Unowned historical/interrupted output preserved; recreate this project with its sources using the updated SDK: " + entry.absoluteFilePath());
        retireScratch(state, entry.absoluteFilePath(), project);
    }
}
void diagnostic(const QString &state, const QString &project, const QString &logs, const QJsonObject &result, bool success) {
    const QString path = state + (success ? "/last-success.json" : "/last-failure.json");
    if (exists(path)) {
        const auto prior = readObject(path);
        require(prior.value("format") == "ARTestDev.NativeDiagnostic.1" && prior.value("project") == project, "Unknown diagnostic preserved: " + path);
    }
    QJsonObject output;
    for (const QString &name : QStringList{"build.log", "metadata-build.log", "generator.log", "integrity.log", "descriptors.log"}) {
        QFile file(logs + '/' + name);
        if (file.open(QIODevice::ReadOnly)) output[name] = QString::fromUtf8(file.read(1024 * 1024));
    }
    writeObject(path, {{"format", "ARTestDev.NativeDiagnostic.1"}, {"project", project}, {"result", result}, {"output", output}});
}
QStringList traceLines(const QString &path) {
    ordinary(path); QFile f(path);
    require(f.open(QIODevice::ReadOnly) && f.size() <= 16 * 1024 * 1024, "Missing dependency trace: " + path);
    const auto data = f.readAll();
    require(data.startsWith(QByteArray::fromHex("fffe")) && data.size() % 2 == 0, "Unsupported dependency trace");
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData() + 2), (data.size() - 2) / 2).split('\n');
}
qint64 buildOutputBytes(const QString &cache) {
    qint64 bytes = 0;
    for (const auto &name : QStringList{"obj", "metaobj", "dll", "metadata"}) {
        QDirIterator files(cache + '/' + name, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (files.hasNext()) { files.next(); ordinary(files.filePath()); bytes += files.fileInfo().size(); }
    }
    return bytes;
}
void invalidateObjects(const QString &cache, const QString &root, const QJsonObject &prior, const QJsonObject &next) {
    const auto traceKey = [](const QString &line) {
        QStringList sources;
        for (const auto &source : line.mid(1).split('|', Qt::SkipEmptyParts)) sources.append(absolute(source).toUpper());
        return '^' + sources.join('|');
    };
    QSet<QString> changed;
    for (auto it = next.begin(); it != next.end(); ++it)
        if (prior.value(it.key()) != it.value()) changed.insert(absolute(root + '/' + it.key()).toUpper());
    for (auto it = prior.begin(); it != prior.end(); ++it)
        if (!next.contains(it.key())) changed.insert(absolute(root + '/' + it.key()).toUpper());
    QDirIterator reads(cache, {"*.read.1.tlog"}, QDir::Files, QDirIterator::Subdirectories);
    while (reads.hasNext()) {
        const QString read = reads.next(); QSet<QString> affected; QString key;
        for (auto line : traceLines(read)) {
            line = line.trimmed().toUpper();
            if (line.startsWith('^')) key = traceKey(line);
            for (QString dependency : line.mid(line.startsWith('^') ? 1 : 0).split('|', Qt::SkipEmptyParts))
                if (changed.contains(absolute(dependency).toUpper())) affected.insert(key);
        }
        const QString tool = QFileInfo(read).fileName().section('.', 0, 0);
        const QString write = QFileInfo(read).absolutePath() + '/' + tool + ".write.1.tlog";
        QMap<QString, QString> objectSources;
        QSet<QString> mappedSources;
        bool mapped = tool.compare("CL", Qt::CaseInsensitive) == 0;
        const QString items = QFileInfo(read).absolutePath() + "/CL.items.tlog";
        if (mapped && exists(items)) {
            ordinary(items); QFile itemFile(items);
            require(itemFile.open(QIODevice::ReadOnly) && itemFile.size() <= 16 * 1024 * 1024, "Unreadable object map: " + items);
            const auto itemBytes = itemFile.readAll();
            const auto itemText = QString::fromUtf8(itemBytes);
            mapped = itemText.toUtf8() == itemBytes;
            for (auto line : itemText.split('\n')) {
                line = line.trimmed();
                if (line.isEmpty()) continue;
                const auto pair = line.split(';');
                if (pair.size() != 2 || !QDir::isAbsolutePath(QDir::fromNativeSeparators(pair[0])) ||
                    !QDir::isAbsolutePath(QDir::fromNativeSeparators(pair[1]))) { mapped = false; break; }
                const QString source = absolute(pair[0]).toUpper(), object = absolute(pair[1]).toUpper();
                if (objectSources.contains(object) || mappedSources.contains(source) ||
                    !object.endsWith(".OBJ") || !object.startsWith(cache.toUpper() + '/')) { mapped = false; break; }
                objectSources.insert(object, source); mappedSources.insert(source);
            }
        } else mapped = false;
        QMap<QString, QStringList> writes;
        for (auto line : traceLines(write)) {
            line = line.trimmed();
            if (line.startsWith('^')) key = traceKey(line);
            else if (!line.isEmpty()) writes[key].append(absolute(line));
        }
        for (auto batch = writes.cbegin(); batch != writes.cend(); ++batch) {
            const auto sources = batch.key().mid(1).split('|', Qt::SkipEmptyParts);
            bool hit = affected.contains(batch.key()), selective = mapped && !hit;
            QSet<QString> batchSources;
            for (const auto &source : sources) {
                hit |= affected.contains('^' + source);
                const auto normalized = absolute(source).toUpper();
                batchSources.insert(normalized);
                selective &= mappedSources.contains(normalized);
            }
            if (!hit) continue;
            // CL.items records explicit /Fo mappings, including renamed objects. Only
            // narrow a grouped write root when every object agrees with that mapping.
            for (const auto &output : batch.value())
                if (output.endsWith(".obj", Qt::CaseInsensitive))
                    selective &= batchSources.contains(objectSources.value(output.toUpper()));
            for (const auto &output : batch.value()) {
                if (tool.compare("CL", Qt::CaseInsensitive) == 0 && !output.endsWith(".obj", Qt::CaseInsensitive)) continue;
                if (selective && !affected.contains('^' + objectSources.value(output.toUpper()))) continue;
                require(output.startsWith(cache + '/', Qt::CaseInsensitive), "Object outside owned intermediates");
                ordinary(output);
                if (exists(output)) require(QFile::remove(output), "Cannot invalidate content-changed object: " + output);
            }
        }
    }
}
void verifyDependencies(const QJsonObject &r, const QString &work) {
    const QString root = QFileInfo(r.value("project").toString()).absolutePath();
    const QString compiler = r.value("toolchain").toString(), windows = r.value("windowsSdk").toString(), v = r.value("windowsSdkVersion").toString();
    const QStringList roots{work, r.value("sdk").toString(), compiler + "/include", compiler + "/lib/x64", compiler + "/bin/Hostx64/x64",
        windows + "/Include/" + v, windows + "/Lib/" + v + "/um/x64", windows + "/Lib/" + v + "/ucrt/x64", windows + "/bin/" + v + "/x64",
        QFileInfo(r.value("msbuild").toString()).absolutePath()};
    const auto under = [](const QString &p, const QString &base) { return p.startsWith(base + '/', Qt::CaseInsensitive); };
    int logs = 0;
    QDirIterator it(work, {"*.read.1.tlog"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next()); ordinary(file.fileName());
        require(file.open(QIODevice::ReadOnly) && file.size() <= 16 * 1024 * 1024, "Missing dependency trace");
        const auto data = file.readAll();
        require(data.startsWith(QByteArray::fromHex("fffe")) && data.size() % 2 == 0, "Unsupported dependency trace");
        const auto text = QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData() + 2), (data.size() - 2) / 2);
        ++logs;
        for (QString line : text.split('\n')) {
            line = line.trimmed(); if (line.startsWith('^')) line.remove(0, 1);
            for (const auto &entry : line.split('|', Qt::SkipEmptyParts)) {
                const QString path = absolute(entry); bool covered = false;
                for (const auto &base : roots) covered |= under(path, base);
                if (under(path, root)) {
                    const auto first = QDir(root).relativeFilePath(path).section('/', 0, 0);
                    covered |= !QStringList{"bin", "obj", "out", ".artest", ".vs", ".git"}.contains(first, Qt::CaseInsensitive);
                }
                const QString system = absolute(qEnvironmentVariable("SystemRoot"));
                // OS loader/localization resources are platform prerequisites, not author source.
                covered |= under(path, system) && QStringList{"dll", "nls", "exe", "mui"}.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
                require(covered, "Dependency outside inventoried roots; place authoring dependencies inside the project: " + path);
                ordinary(path);
            }
        }
    }
    require(logs >= 4, "Compiler/linker dependency tracking is required for both native child builds");
}
void publicationPause(const QString &phase) {
#ifdef ARTESTDEV_TESTING
    if (qEnvironmentVariable("ARTESTDEV_FAULT_PHASE") == phase) {
        const QString marker = qEnvironmentVariable("ARTESTDEV_FAULT_MARKER");
        if (!marker.isEmpty()) { QFile f(marker); require(f.open(QIODevice::WriteOnly), "Fault marker"); f.write(phase.toUtf8()); f.close(); }
        Sleep(INFINITE);
    }
#else
    Q_UNUSED(phase);
#endif
}
void promote(const QString &state, const QString &project, const QString &assembled, QJsonObject j) {
    const QString journal = state + "/transaction.json", candidate = state + "/candidate", current = state + "/current", backup = state + "/backup";
    j["candidateOwner"] = owned(assembled, project);
    if (exists(current)) j["previousOwner"] = owned(current, project);
    j["phase"] = "staged"; j["assembled"] = assembled; writeObject(journal, j);
    move(assembled, candidate);
    j["phase"] = "ready"; writeObject(journal, j);
    publicationPause("ready");
    if (exists(current)) move(current, backup);
    publicationPause("backup-renamed");
    j["phase"] = "backed-up"; writeObject(journal, j);
    publicationPause("before-promotion");
    move(candidate, current);
    publicationPause("current-renamed");
    j["phase"] = "promoted"; writeObject(journal, j);
    recover(state, project);
}
QJsonObject requestChecked(QJsonObject r) {
    for (const QString &key : {QStringLiteral("project"), QStringLiteral("sdk"), QStringLiteral("msbuild"), QStringLiteral("toolchain"), QStringLiteral("windowsSdk")}) {
        const QString p = r.value(key).toString(); require(QDir::isAbsolutePath(p) && !p.contains(QRegularExpression("[$%;]")), "Use absolute MSBuild paths without $, % or ;: " + key); r[key] = absolute(p); ordinary(p);
    }
    require(r.value("platform") == "x64", "Only x64 is supported");
    const QString project = r.value("project").toString(), root = QFileInfo(project).absolutePath();
    const auto p = readObject(root + "/artest-sdk-project.json");
    require(p.value("language") == "cpp" && p.value("projectFile") == QFileInfo(project).fileName(), "Not a generated native project");
    require(!root.startsWith(r.value("sdk").toString() + '/', Qt::CaseInsensitive), "Project must be outside SDK");
    const auto kit = inspectStagingExecutable(r.value("sdk").toString() + "/../ARTestDev.exe");
    require(kit.valid, kit.diagnostics.join('\n'));
    r["extensionId"] = p.value("extensionId");
    return r;
}
}
#ifdef ARTESTDEV_TESTING
void publishFixture(const QString &state, const QString &project, const QString &revision) {
    Lock lock(state + "/operation.lock");
    recover(state, project);
    if (revision == "input-snapshot") {
        InputSnapshot captured; snapshot = &captured; inputHashing = true;
        const auto reset = qScopeGuard([] { snapshot = nullptr; inputHashing = false; });
        const auto initial = digest(project);
        publicationPause("input-snapshot");
        require(digest(project) == initial, "Changed snapshot");
        return;
    }
    if (revision == "retire") {
        resumeRetirement(state, project);
        if (exists(state + "/intermediates")) retireScratch(state, state + "/intermediates", project);
        return;
    }
    if (revision == "recover") return;
    QJsonObject j{{"format", "ARTestDev.NativeTransaction.1"}, {"project", project}, {"state", state}, {"revision", revision}, {"previous", exists(state + "/current")}, {"phase", "building"}, {"retentionProfile", 1}};
    promote(state, project, state + "/work-" + revision + "/assembled", j);
}
#endif
QJsonObject inventory(const QString &root, bool sources) { QJsonObject out; scan(absolute(root), absolute(root), out, sources); return out; }
QJsonObject inputs(const QJsonObject &request) {
    const auto r = request;
    const QString version = r.value("windowsSdkVersion").toString();
    require(QRegularExpression("^10\\.[0-9]+\\.[0-9]+\\.[0-9]+$").match(version).hasMatch(), "Invalid Windows SDK version");
    const QString evaluation = stateRoot(r) + "/evaluation-" + QUuid::createUuid().toString(QUuid::Id128);
    mkdir(evaluation);
    const QString project = r.value("project").toString();
    auto evaluated = QJsonDocument::fromJson(run(r.value("msbuild").toString(), {project, "/nologo", "/nr:false", "/pp:" + evaluation + "/project.xml", "/getItem:ClCompile,ClInclude,ARTestDevInput",
        "/p:ARTestDevInner=true;PreferredToolArchitecture=x64;Configuration=" + r.value("configuration").toString() +
        ";Platform=x64;ARTestSDKRoot=" + r.value("sdk").toString() + ";VCToolsVersion=" + QFileInfo(r.value("toolchain").toString()).fileName() +
        ";WindowsTargetPlatformVersion=" + version}, QFileInfo(project).absolutePath(), evaluation + "/evaluation.log")).object();
    require(evaluated.value("Items").isObject(), "MSBuild did not return evaluated dependencies");
    auto items = evaluated.value("Items").toObject();
    for (auto kind = items.begin(); kind != items.end(); ++kind) {
        auto rows = kind.value().toArray();
        for (qsizetype i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toObject();
            // Reading and compilation update access times. Content hashes, not bookkeeping
            // timestamps, establish identity; evaluated compiler options remain in the record.
            row.remove("AccessedTime"); row.remove("CreatedTime"); row.remove("ModifiedTime");
            rows[i] = row;
        }
        kind.value() = rows;
    }
    evaluated["Items"] = items;
    inputHashing = true; const auto resetHashing = qScopeGuard([] { inputHashing = false; });
    const QString root = QFileInfo(project).absolutePath();
    auto sources = inventory(root, true);
    if (snapshot) {
        if (snapshot->projectFiles.isEmpty()) snapshot->projectFiles = sources;
        else require(snapshot->projectFiles == sources, "Inputs changed during build; previous result preserved (project inventory)");
    }
    QSet<QString> declaredPaths;
    QJsonObject declared;
    for (const auto &kind : QStringList{"ClCompile", "ClInclude", "ARTestDevInput"}) {
        for (const auto &item : evaluated.value("Items").toObject().value(kind).toArray()) {
            const QString path = absolute(item.toObject().value("FullPath").toString());
            require(path.startsWith(root + '/', Qt::CaseInsensitive), "Declared author input must be inside the project: " + path);
            declaredPaths.insert(QDir(root).relativeFilePath(path));
            if (kind == "ARTestDevInput") declared[path] = digest(path);
        }
    }
    const QString cache = stateRoot(r) + "/intermediates";
    if (!exists(cache) && exists(stateRoot(r) + "/current/inputs.json")) {
        for (const auto &name : readObject(stateRoot(r) + "/current/inputs.json").value("sources").toObject().keys()) declaredPaths.insert(name);
    }
    QDirIterator traces(cache, {"*.read.1.tlog"}, QDir::Files, QDirIterator::Subdirectories);
    while (traces.hasNext()) {
        for (auto line : traceLines(traces.next())) {
            line = line.trimmed(); if (line.startsWith('^')) line.remove(0, 1);
            for (const auto &entry : line.split('|', Qt::SkipEmptyParts)) {
                const QString path = absolute(entry);
                if (path.startsWith(root + '/', Qt::CaseInsensitive) && !path.startsWith(root + "/.artest/", Qt::CaseInsensitive)) {
                    const QString relative = QDir(root).relativeFilePath(path);
                    for (const auto &name : sources.keys()) if (name.compare(relative, Qt::CaseInsensitive) == 0) declaredPaths.insert(name);
                }
            }
        }
    }
    // Non-build documents are ignored unless explicitly declared as native inputs.
    QSet<QString> dependencyNames;
    for (const auto &path : declaredPaths) dependencyNames.insert(QFileInfo(path).fileName().toLower());
    for (const auto &name : sources.keys()) {
        if (name.endsWith('/') || (!declaredPaths.contains(name) && !dependencyNames.contains(QFileInfo(name).fileName().toLower()) &&
            QStringList{"json", "md", "txt"}.contains(QFileInfo(name).suffix(), Qt::CaseInsensitive))) sources.remove(name);
    }
    QJsonObject out{{"request", r}, {"sources", sources}, {"declaredInputs", declared}, {"evaluatedItems", evaluated},
        {"sdk", inventory(absolute(r.value("sdk").toString() + "/.."))},
        {"msbuild", digest(r.value("msbuild").toString())}};
    if (!snapshot || !snapshot->kitChecked) {
        const auto kit = inspectStagingExecutable(r.value("sdk").toString() + "/../ARTestDev.exe");
        require(kit.valid, kit.diagnostics.join('\n'));
        if (snapshot) snapshot->kitChecked = true;
    }
    inputHashing = false;
    out["evaluatedImports"] = digest(evaluation + "/project.xml");
    sealScratch(evaluation, r.value("project").toString());
    inputHashing = true;
    out["msbuildTools"] = inventory(QFileInfo(r.value("msbuild").toString()).absolutePath());
    const QString toolchain = r.value("toolchain").toString();
    out["compiler"] = inventory(toolchain + "/bin/Hostx64/x64");
    out["compilerHeaders"] = inventory(toolchain + "/include");
    out["compilerLibraries"] = inventory(toolchain + "/lib/x64");
    const QString sdk = r.value("windowsSdk").toString(), v = r.value("windowsSdkVersion").toString();
    require(QRegularExpression("^10\\.[0-9]+\\.[0-9]+\\.[0-9]+$").match(v).hasMatch(), "Invalid Windows SDK version");
    out["windowsHeaders"] = inventory(sdk + "/Include/" + v);
    out["windowsUmLibraries"] = inventory(sdk + "/Lib/" + v + "/um/x64");
    out["windowsCrtLibraries"] = inventory(sdk + "/Lib/" + v + "/ucrt/x64");
    out["windowsTools"] = inventory(sdk + "/bin/" + v + "/x64");
    QJsonObject environment;
    const auto env = QProcessEnvironment::systemEnvironment();
    for (const QString &name : QStringList{"CL", "_CL_", "LINK", "_LINK_", "INCLUDE", "LIB", "LIBPATH", "VCToolsVersion", "WindowsSDKVersion"}) environment[name] = env.value(name);
    out["environment"] = environment;
    return out;
}
void recover(const QString &state, const QString &project) {
    checkState(state);
    const QString journal = state + "/transaction.json", current = state + "/current", candidate = state + "/candidate", backup = state + "/backup";
    if (!exists(journal)) { require(!exists(candidate) && !exists(backup), "Unowned recovery directories preserved"); return; }
    auto j = readObject(journal);
    require(j.value("format") == "ARTestDev.NativeTransaction.1" && j.value("project") == project && j.value("state") == state, "Unknown transaction preserved");
    QString phase = j.value("phase").toString();
    const bool previous = j.value("previous").toBool();
    const QString revision = j.value("revision").toString(), work = state + "/work-" + revision;
    require(j.value("previous").isBool() && QRegularExpression("^[0-9a-f]{32}$").match(revision).hasMatch(), "Invalid publication identity");
    const auto next = j.value("candidateOwner").toObject(), prior = j.value("previousOwner").toObject();
    require(!next.isEmpty() && next.value("revision") == revision && (!previous || !prior.isEmpty()), "Missing publication ownership identities; preserve transaction for inspection");
    const auto matches = [&](const QString &path, const QJsonObject &owner) {
        require(owned(path, project) == owner, "Publication ownership identity differs: " + path);
    };
    const auto setPhase = [&](const QString &value) { j["phase"] = value; writeObject(journal, j); phase = value; };
    const QString retiredCandidate = work + "/retired-candidate", retiredBackup = work + "/retired-backup";
    const bool c = exists(current), n = exists(candidate), b = exists(backup);
    if (phase == "staged") {
        const QString assembled = j.value("assembled").toString();
        require(assembled == work + "/assembled", "Invalid assembled path");
        require(!b && c == previous && n != exists(assembled), "Ambiguous staging topology");
        if (c) matches(current, prior);
        if (!n) {
            matches(assembled, next);
            if (j.value("retentionProfile") == 1) {
                auto entries = QDir(work).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
                if (entries.contains("scratch.json")) { verifyScratch(work, project); entries.removeAll("scratch.json"); }
                require(entries == QStringList{"assembled"}, "Unknown staging content preserved");
                sealScratch(work, project);
            }
            require(QFile::remove(journal), "Cannot retire staged journal"); return;
        }
        phase = "ready";
    }
    if (phase == "ready" || phase == "backed-up") {
        if (n && ((c == previous && !b) || (previous && !c && b))) {
            matches(candidate, next);
            if (c) matches(current, prior);
            if (b) matches(backup, prior);
            require(!exists(retiredCandidate) && !exists(retiredBackup), "Unexpected retired outputs");
            setPhase("rolling-back");
        } else if (phase == "backed-up" && c && !n && b == previous) {
            matches(current, next);
            if (b) matches(backup, prior);
            setPhase("promoted");
        } else require(false, "Ambiguous publication topology preserved");
    }
    if (phase == "rolling-back" || phase == "rolled-back") {
        // Persist intent before either rename. Full ownership identities distinguish equal payloads.
        require(!exists(retiredBackup) && (previous ? c != b : !c && !b) && n != exists(retiredCandidate), "Ambiguous rollback topology");
        if (c) matches(current, prior);
        if (b) matches(backup, prior);
        matches(n ? candidate : retiredCandidate, next);
        if (b) move(backup, current);
        publicationPause("rollback-restored");
        if (n) { mkdir(work); move(candidate, retiredCandidate); }
        publicationPause("candidate-retired");
        setPhase("rolled-back");
    } else if (phase == "promoted" || phase == "retiring-backup" || phase == "completed") {
        require(c && !n && !exists(retiredCandidate) && (previous ? b != exists(retiredBackup) : !b && !exists(retiredBackup)), "Ambiguous promotion cleanup topology");
        matches(current, next);
        if (previous) matches(b ? backup : retiredBackup, prior);
        setPhase("retiring-backup");
        // Retain verified trees by rename: interruption cannot leave a partially deleted inventory.
        if (b) { mkdir(work); move(backup, retiredBackup); }
        publicationPause("backup-retired");
        setPhase("completed");
    } else require(false, "Ambiguous/interrupted build preserved; inspect transaction before retrying: " + journal);
    publicationPause("before-journal-retirement");
    if (j.value("retentionProfile") == 1 && exists(work)) {
        if (exists(work + "/scratch.json")) verifyScratch(work, project);
        for (const auto &entry : QDir(work).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
            if (entry.fileName() == "scratch.json") continue;
            require(entry.isDir() && QStringList{"assembled", "retired-backup", "retired-candidate"}.contains(entry.fileName()), "Unknown work content preserved");
            matches(entry.absoluteFilePath(), entry.fileName() == "retired-backup" ? prior : next);
        }
        sealScratch(work, project);
        publicationPause("work-sealed");
    }
    require(QFile::remove(journal), "Cannot retire transaction");
}
QJsonObject inspect(const QJsonObject &request) {
    InputSnapshot captured; snapshot = &captured;
    const auto clearSnapshot = qScopeGuard([] { snapshot = nullptr; inputHashing = false; });
    const auto r = requestChecked(request); const QString state = stateRoot(r);
    require(exists(state), "Missing build; compile in Visual Studio"); Lock lock(state + "/operation.lock"); checkState(state);
    require(!exists(state + "/transaction.json"), "Recovery required; build in Visual Studio before validation");
    prune(state, r.value("project").toString());
    const auto owner = owned(state + "/current", r.value("project").toString());
    writeObject(state + "/transaction.json", {{"format", "ARTestDev.NativeTransaction.1"}, {"project", r.value("project")}, {"state", state}, {"phase", "checking"}, {"previous", true}});
    const auto finishCheck = qScopeGuard([&] {
        QFile::remove(state + "/transaction.json");
        try { prune(state, r.value("project").toString()); } catch (const std::exception &) { }
    });
    require(readObject(state + "/current/inputs.json").value("request").toObject() == r, "Stale SDK/configuration/toolchain selection; build in Visual Studio");
    require(owner.value("inputIdentity") == identity(inputs(r)), "Stale source, SDK, configuration or toolchain; build in Visual Studio");
    require(QFile::remove(state + "/transaction.json"), "Inspection complete but journal cleanup failed; recovery required");
    prune(state, r.value("project").toString());
    return {{"status", "compiled"}, {"revision", owner.value("revision")}, {"package", state + "/current/package"}, {"inputIdentity", owner.value("inputIdentity")}};
}
QJsonObject build(const QJsonObject &request, bool rebuild) {
    QElapsedTimer timer; timer.start(); hashedFiles = 0; hashedBytes = 0;
    QJsonObject timing;
    InputSnapshot captured; snapshot = &captured;
    const auto clearSnapshot = qScopeGuard([] { snapshot = nullptr; inputHashing = false; });
    const auto r = requestChecked(request); const QString project = r.value("project").toString(), root = QFileInfo(project).absolutePath(), state = stateRoot(r);
    mkdir(state); Lock lock(state + "/operation.lock"); recover(state, project); prune(state, project);
    const QString current = state + "/current", journal = state + "/transaction.json";
    const QString cache = state + "/intermediates";
    if (exists(cache)) verifyScratch(cache, project);
    if (exists(current)) owned(current, project);
    const QString revision = QUuid::createUuid().toString(QUuid::Id128);
    QJsonObject j{{"format", "ARTestDev.NativeTransaction.1"}, {"project", project}, {"state", state}, {"revision", revision}, {"previous", exists(current)}, {"phase", "building"}, {"retentionProfile", 1}};
    writeObject(journal, j);
    bool publicationStarted = false;
    const auto retainFailure = qScopeGuard([&] {
        if (!publicationStarted) {
            if (exists(state + "/retirement.json")) return;
            // run() does not return before settlement. A killed supervisor never executes this guard.
            if (std::uncaught_exceptions() > 0) {
                try {
                    diagnostic(state, project, cache, {{"status", "failed"}, {"revision", revision}}, false);
                    if (exists(cache)) {
                        if (exists(cache + "/inputs.json")) require(QFile::remove(cache + "/inputs.json"), "Cannot invalidate failed intermediates");
                        sealScratch(cache, project);
                    }
                    const QString failedWork = state + "/work-" + revision;
                    if (exists(failedWork)) sealScratch(failedWork, project);
                } catch (const std::exception &) { return; }
            }
            QFile::remove(journal);
            try { prune(state, project); } catch (const std::exception &) { /* Retain the verifiable cleanup intent for retry. */ }
        }
    });
    auto before = inputs(r); QString id = identity(before);
    timing["evaluationHashMs"] = timer.elapsed(); timer.restart();
    if (!rebuild && exists(current) && exists(cache + "/inputs.json") && readObject(current + "/inputs.json") == before && readObject(cache + "/inputs.json") == before) {
        const auto owner = owned(current, project);
        require(QFile::remove(journal), "Cannot retire unchanged check");
        prune(state, project);
        timing["hashedFiles"] = hashedFiles; timing["hashedBytes"] = hashedBytes;
        timing["dllMs"] = 0; timing["metadataMs"] = 0; timing["publicationMs"] = 0;
        return {{"status", "compiled"}, {"reused", true}, {"revision", owner.value("revision")}, {"inputIdentity", id}, {"package", current + "/package"}, {"metrics", timing}};
    }
    bool full = rebuild || !exists(cache + "/inputs.json") || !exists(cache + "/budget.json");
    if (!full) {
        auto previous = readObject(cache + "/inputs.json"), next = before;
        const auto oldSources = previous.take("sources").toObject(), newSources = next.take("sources").toObject();
        // A new/deleted include candidate can change resolution even when absent from the old read trace.
        full = previous != next || oldSources.keys() != newSources.keys();
        if (!full) invalidateObjects(cache, root, oldSources, newSources);
    }
    if (full && exists(cache)) {
        // Evaluation has settled and compilation has not started. Retire obsolete intermediates
        // with the independent deletion protocol; current remains the rollback source.
        require(QFile::remove(journal), "Cannot checkpoint intermediate reset");
        retireScratch(state, cache, project);
        writeObject(journal, j);
    }
    mkdir(cache);
    const QString work = state + "/work-" + revision; mkdir(work);
    const QString common = "/p:ARTestDevInner=true;PreferredToolArchitecture=x64;BuildProjectReferences=false;BuildingSolutionFile=false;Configuration=" + r.value("configuration").toString() + ";Platform=x64;ARTestSDKRoot=" + r.value("sdk").toString() + ";VCToolsInstallDir=" + r.value("toolchain").toString() + "/;VCToolsVersion=" + QFileInfo(r.value("toolchain").toString()).fileName() + ";WindowsSdkDir=" + r.value("windowsSdk").toString() + "/;WindowsTargetPlatformVersion=" + r.value("windowsSdkVersion").toString();
    const QString msbuild = r.value("msbuild").toString();
    const QString name = r.value("targetName").toString();
    require(QRegularExpression("^[A-Za-z][A-Za-z0-9_-]*$").match(name).hasMatch(), "Unsupported target name");
    qint64 dllMs = 0, metadataMs = 0;
    const auto compile = [&](const QString &target) {
        timer.restart();
        run(msbuild, {project, target, "/m:1", "/nr:false", "/v:minimal", common,
            "/p:OutDir=" + cache + "/dll/;IntDir=" + cache + "/obj/"}, root, cache + "/build.log", r.value("toolchain").toString() + "/bin/Hostx64/x64/vctip.exe");
        dllMs += timer.elapsed(); timer.restart();
        run(msbuild, {project, target, "/m:1", "/nr:false", "/v:minimal", common,
            "/p:ARTestMetadataBuild=true;ConfigurationType=Application;TargetName=" + name + "Metadata;TargetExt=.exe;OutDir=" + cache + "/metadata/;IntDir=" + cache + "/metaobj/"}, root, cache + "/metadata-build.log", r.value("toolchain").toString() + "/bin/Hostx64/x64/vctip.exe");
        metadataMs += timer.elapsed();
    };
    compile(full ? "/t:Rebuild" : "/t:Build");
    qint64 freshBytes = buildOutputBytes(cache);
    bool compacted = false;
    if (!full) {
        const auto budget = readObject(cache + "/budget.json");
        require(budget.value("format") == "ARTestDev.NativeCacheBudget.1" && budget.value("project") == project &&
            budget.value("freshBytes").isDouble() && budget.value("freshBytes").toDouble() > 0, "Invalid native cache budget");
        freshBytes = budget.value("freshBytes").toInteger();
        // MSVC PDBs retain prior edit data. Bound that history, while retaining reusable
        // objects between compactions. Rebuild cleans compiler-owned outputs under the
        // existing transaction; interruption still preserves current and recovery state.
        if (buildOutputBytes(cache) > freshBytes + qMax(8LL * 1024 * 1024, freshBytes / 2)) {
            compile("/t:Rebuild"); compacted = true;
            freshBytes = buildOutputBytes(cache);
        }
    }
    writeObject(cache + "/budget.json", {{"format", "ARTestDev.NativeCacheBudget.1"}, {"project", project}, {"freshBytes", freshBytes}});
    timing["compacted"] = compacted; timing["dllMs"] = dllMs;
    timer.restart();
    verifyDependencies(r, cache);
    const QString generator = cache + "/metadata/" + name + "Metadata.exe";
    const QJsonObject generatorInput{{"executable", digest(generator)}, {"name", name}, {"declaredInputs", before.value("declaredInputs")}};
    QJsonObject bundle;
    if (!rebuild && exists(cache + "/generator-input.json") && readObject(cache + "/generator-input.json") == generatorInput)
        bundle = readObject(cache + "/metadata-bundle.json");
    else {
        bundle = QJsonDocument::fromJson(run(generator, {name + ".dll"}, root, cache + "/generator.log")).object();
        writeObject(cache + "/metadata-bundle.json", bundle);
        writeObject(cache + "/generator-input.json", generatorInput);
    }
    timing["metadataMs"] = metadataMs + timer.elapsed(); timer.restart();
    require(bundle.value("format") == "ARTest.MetadataBundle" && bundle.value("version") == 1 && bundle.value("schemas").isObject(), "Invalid metadata bundle");
    auto manifest = QJsonDocument::fromJson(bundle.value("manifestText").toString().toUtf8()).object();
    require(manifest == bundle.value("manifest").toObject() && manifest.value("extensionId") == r.value("extensionId") && manifest.value("runtime").toObject().value("entry") == name + ".dll", "Metadata identity mismatch");
    const auto after = inputs(r);
    auto beforeOther = before, afterOther = after; beforeOther.remove("sources"); afterOther.remove("sources");
    QStringList differences;
    for (auto it = beforeOther.begin(); it != beforeOther.end(); ++it) if (afterOther.value(it.key()) != it.value()) differences << it.key();
    const auto oldItems = before.value("evaluatedItems").toObject().value("Items").toObject();
    const auto newItems = after.value("evaluatedItems").toObject().value("Items").toObject();
    for (auto kind = oldItems.begin(); kind != oldItems.end(); ++kind) {
        const auto oldRows = kind.value().toArray(), newRows = newItems.value(kind.key()).toArray();
        for (qsizetype i = 0; i < oldRows.size() && i < newRows.size(); ++i) {
            const auto row = oldRows[i].toObject(), nextRow = newRows[i].toObject();
            for (auto field = row.begin(); field != row.end(); ++field)
                if (field.value() != nextRow.value(field.key()) && differences.size() < 12)
                    differences << kind.key() + '.' + field.key() + ": " + field.value().toString().left(200) + " -> " + nextRow.value(field.key()).toString().left(200);
        }
    }
    require(beforeOther == afterOther, "Inputs changed during build; previous result preserved: " + differences.join(", "));
    // Newly observed include dependencies were already hashed and write-locked in the complete source snapshot.
    before = after; id = identity(before);
    timing["verificationMs"] = timer.elapsed(); timer.restart();
    const QString assembled = work + "/assembled";
    mkdir(assembled + "/package/schemas");
    const auto schemas = bundle.value("schemas").toObject();
    for (auto it = schemas.begin(); it != schemas.end(); ++it) {
        require(QRegularExpression("^schemas/[a-z0-9]+([.-][a-z0-9]+)*\\.json$").match(it.key()).hasMatch() && it.value().isString(), "Unsafe generated schema");
        QJsonParseError e; const auto d = QJsonDocument::fromJson(it.value().toString().toUtf8(), &e);
        require(e.error == QJsonParseError::NoError && d.isObject(), "Malformed schema output");
        writeObject(assembled + "/package/" + it.key(), d.object());
    }
    require(QFile::copy(cache + "/dll/" + name + ".dll", assembled + "/package/" + name + ".dll"), "Cannot copy compiled DLL");
    manifest["integrity"] = QJsonObject{{"sha256", digest(assembled + "/package/" + name + ".dll")}};
    writeObject(assembled + "/package/artest-extension.json", manifest);
    writeObject(assembled + "/inputs.json", before);
    writeObject(assembled + "/ownership.json", {{"format", "ARTestDev.NativeOutput.1"}, {"project", project}, {"extensionId", r.value("extensionId")}, {"revision", revision}, {"inputIdentity", id}, {"files", inventory(assembled)}});
    j["candidateOwner"] = owned(assembled, project);
    if (exists(current)) j["previousOwner"] = owned(current, project);
    publicationStarted = true;
    writeObject(cache + "/inputs.json", before); sealScratch(cache, project);
    promote(state, project, assembled, j);
    prune(state, project);
    timing["publicationMs"] = timer.elapsed();
    timing["hashedFiles"] = hashedFiles; timing["hashedBytes"] = hashedBytes;
    QJsonObject result{{"status", "compiled"}, {"revision", revision}, {"inputIdentity", id}, {"package", current + "/package"}, {"metrics", timing}};
    diagnostic(state, project, cache, result, true);
    return result;
}
QJsonObject clean(const QJsonObject &request) {
    const auto r = requestChecked(request); const QString state = stateRoot(r), project = r.value("project").toString();
    mkdir(state); Lock lock(state + "/operation.lock"); checkState(state);
    require(!exists(state + "/transaction.json") && !exists(state + "/retirement.json"), "Recovery pending; Clean preserves all state");
    prune(state, project);
    if (exists(state + "/intermediates")) retireScratch(state, state + "/intermediates", project);
    return {{"status", "cleaned"}, {"diagnostic", "Owned intermediates removed; published package and configuration preserved."}, {"package", state + "/current/package"}};
}
QJsonObject validate(const QJsonObject &request, const QString &cli) {
    InputSnapshot captured; snapshot = &captured;
    const auto clearSnapshot = qScopeGuard([] { snapshot = nullptr; inputHashing = false; });
    const auto r = requestChecked(request); const QString state = stateRoot(r), project = r.value("project").toString();
    require(exists(state), "Missing build; compile in Visual Studio"); Lock lock(state + "/operation.lock"); checkState(state);
    require(!exists(state + "/transaction.json"), "Recovery required before target validation");
    prune(state, project);
    const auto owner = owned(state + "/current", project);
    writeObject(state + "/transaction.json", {{"format", "ARTestDev.NativeTransaction.1"}, {"project", project}, {"state", state}, {"phase", "validating"}, {"previous", true}});
    QString logs;
    const auto finishValidation = qScopeGuard([&] {
        if (exists(state + "/retirement.json")) return;
        try {
            if (!logs.isEmpty() && exists(logs)) {
                if (std::uncaught_exceptions() > 0) diagnostic(state, project, logs, {{"status", "target-validation-failed"}}, false);
                sealScratch(logs, project);
            }
            QFile::remove(state + "/transaction.json");
            prune(state, project);
        } catch (const std::exception &) { }
    });
    require(readObject(state + "/current/inputs.json").value("request").toObject() == r, "Stale SDK/configuration/toolchain selection; build in Visual Studio");
    require(QDir::isAbsolutePath(cli) && QFileInfo(cli).isFile() && QFileInfo(QFileInfo(cli).absolutePath() + "/ARTestEngine.dll").isFile(), "Select an existing separate CLI/Engine installation");
    const auto before = inputs(r);
    require(owner.value("inputIdentity") == identity(before), "Stale build; compile in Visual Studio");
    ordinary(cli); require(QDir::isAbsolutePath(cli) && QFileInfo(cli).fileName().compare("ARTestCLI.exe", Qt::CaseInsensitive) == 0, "Select an explicit separate ARTestCLI.exe");
    const QString target = QFileInfo(cli).absolutePath();
    const auto nested = [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) == 0 || a.startsWith(b + '/', Qt::CaseInsensitive); };
    const QString sourceRoot = QFileInfo(project).absolutePath(), kitRoot = absolute(r.value("sdk").toString() + "/..");
    require(!nested(target, sourceRoot) && !nested(sourceRoot, target) && !nested(target, kitRoot) && !nested(kitRoot, target), "Runtime must be separate from project and SDK");
    const QJsonObject runtime{{"cli", digest(cli)}, {"engine", digest(target + "/ARTestEngine.dll")}};
    logs = state + "/validation-" + QUuid::createUuid().toString(QUuid::Id128); mkdir(logs);
    const auto checked = QJsonDocument::fromJson(run(cli, {"extensions", "validate", state + "/current"}, target, logs + "/integrity.log")).object();
    require(checked.value("schema") == "artest.schema.extension-catalog.v2" && checked.value("valid").isBool() && checked.value("valid").toBool() && checked.value("packages").toArray().size() == 1 && checked.value("abi").toObject() == QJsonObject{{"major", 0}, {"minor", 2}}, "Incompatible CLI or invalid package report");
    const QByteArray doctor = run(cli, {"extensions", "doctor", state + "/current"}, target, logs + "/descriptors.log");
    // Doctor emits Engine diagnostic lines before the final JSON snapshot.
    const qsizetype start = doctor.indexOf('{'); const auto report = QJsonDocument::fromJson(doctor.mid(start)).object();
    require(start >= 0 && report.value("valid").isBool() && report.value("valid").toBool() && report.value("status") == "active" && report.value("packages").toArray().size() == 1, "Descriptor validation did not return an active valid catalog");
    require(inputs(r) == before && owned(state + "/current", project) == owner && digest(cli) == runtime.value("cli") && digest(target + "/ARTestEngine.dll") == runtime.value("engine"), "Inputs/outputs/target changed during validation");
    QJsonObject result{{"status", "target-validated"}, {"revision", owner.value("revision")}, {"target", absolute(cli)}, {"runtime", runtime}, {"inputIdentity", owner.value("inputIdentity")}, {"logs", inventory(logs)}};
    writeObject(logs + "/result.json", result);
    diagnostic(state, project, logs, result, true);
    sealScratch(logs, project);
    require(QFile::remove(state + "/transaction.json"), "Target validation complete but journal cleanup failed; recovery required");
    prune(state, project);
    return result;
}
}

#include "RegistrationStore.h"
#include "../ThirdParty/nlohmann/json.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QDateTime>
#include <QStandardPaths>
#include <QRegularExpression>
#include <stdexcept>
#include <set>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ARTestDev::Registration {
void require(bool ok, const QString &message) { if (!ok) throw std::runtime_error(message.toUtf8().constData()); }
QString absolute(const QString &p) { return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(p).absoluteFilePath())); }
void ordinary(const QString &path) {
    require(QDir::isAbsolutePath(path) && !path.startsWith("//"), "Seleccione una ruta local absoluta: " + path);
    for (QString p = absolute(path);;) {
        const auto a = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(p).utf16()));
        require(a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_REPARSE_POINT), "Reparse point: conserve e inspeccione " + p);
        const auto parent = QFileInfo(p).absolutePath(); if (parent == p) break; p = parent;
    }
}
QByteArray read(const QString &p) {
    ordinary(p); QFile f(p); require(f.open(QIODevice::ReadOnly) && f.size() <= 8 * 1024 * 1024, "No se puede leer (limite 8 MiB): " + p); const auto bytes = f.read(8 * 1024 * 1024 + 1); require(bytes.size() <= 8 * 1024 * 1024 && f.atEnd(), "JSON cambio/excede limite: " + p); return bytes;
}
QJsonObject object(const QByteArray &b) {
    require(b.size() <= 8 * 1024 * 1024, "JSON excede 8 MiB");
    std::vector<std::set<std::string>> keys;
    auto callback = [&keys](int depth, nlohmann::json::parse_event_t event, nlohmann::json &value) {
        require(depth < 64, "JSON demasiado profundo");
        if (event == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
        if (event == nlohmann::json::parse_event_t::key) require(keys.back().insert(value.get<std::string>()).second, "JSON ambiguo: clave duplicada");
        if (event == nlohmann::json::parse_event_t::object_end) keys.pop_back();
        return true;
    };
    const auto checked = nlohmann::json::parse(b.constData(), b.constData() + b.size(), callback);
    require(checked.is_object(), "Se requiere un objeto JSON");
    QJsonParseError error; const auto d = QJsonDocument::fromJson(b, &error);
    require(error.error == QJsonParseError::NoError && d.isObject(), "JSON invalido"); return d.object();
}
QJsonObject load(const QString &p) { return object(read(p)); }
void write(const QString &p, const QByteArray &b) {
    ordinary(p); require(QDir().mkpath(QFileInfo(p).absolutePath()), "Carpeta sin permiso de escritura: " + p);
    QSaveFile f(p); f.setDirectWriteFallback(false);
    require(f.open(QIODevice::WriteOnly) && f.write(b) == b.size() && f.commit(), "No se puede publicar; compruebe permisos: " + p);
}
void save(const QString &p, const QJsonObject &v) { write(p, QJsonDocument(v).toJson(QJsonDocument::Compact)); }
QString hash(const QByteArray &b) { return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex()); }
QString digest(const QString &p) {
    ordinary(p); QFile f(p); QCryptographicHash h(QCryptographicHash::Sha256);
    require(f.open(QIODevice::ReadOnly) && f.size() <= 512LL * 1024 * 1024, "No se puede verificar (limite 512 MiB): " + p);
    const auto expected = f.size(); qint64 remaining = expected;
    while (remaining > 0) { const auto chunk = f.read(qMin<qint64>(remaining, 1024 * 1024)); require(!chunk.isEmpty(), "Lectura incompleta: " + p); h.addData(chunk); remaining -= chunk.size(); }
    require(f.atEnd() && f.size() == expected, "Archivo cambio durante hash: " + p);
    return QString::fromLatin1(h.result().toHex());
}
QJsonObject inventory(const QString &path) {
    ordinary(path); require(QFileInfo(path).isDir(), "Falta el directorio: " + path);
    QJsonObject result; QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    int count = 0; qint64 bytes = 0;
    while (it.hasNext()) {
        const auto p = it.next(); ordinary(p); const QFileInfo f(p);
        require(++count <= 8192 && (bytes += f.isFile() ? f.size() : 0) <= 2LL * 1024 * 1024 * 1024, "Inventario excede 8192 entradas/2 GiB; conserve el destino");
        if (f.isFile()) result[QDir(path).relativeFilePath(p)] = digest(p);
        else require(f.isDir(), "Archivo no regular: " + p);
    }
    return result;
}
void copy(const QString &from, const QString &to) {
    const auto before = inventory(from); ordinary(to); require(!QFileInfo::exists(to), "Destino ocupado: " + to);
    require(QDir().mkpath(to), "No se puede crear: " + to);
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    int count = 0;
    while (it.hasNext()) {
        const auto p = it.next(); ordinary(p); require(++count <= 8192, "La fuente cambio durante copia");
        const auto dest = to + '/' + QDir(from).relativeFilePath(p); ordinary(dest);
        if (QFileInfo(p).isDir()) require(QDir().mkpath(dest), "No se puede crear: " + dest);
        else { require(QDir().mkpath(QFileInfo(dest).absolutePath()), "No se puede crear carpeta"); QFile source(p), output(dest);
            require(source.open(QIODevice::ReadOnly) && source.size() <= 512LL * 1024 * 1024 && output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "No se puede copiar: " + p);
            const auto expected = source.size(); qint64 remaining = expected;
            while (remaining > 0) { const auto chunk = source.read(qMin<qint64>(remaining, 1024 * 1024)); require(!chunk.isEmpty() && output.write(chunk) == chunk.size(), "Copia incompleta: " + p); remaining -= chunk.size(); }
            require(source.atEnd() && source.size() == expected && output.flush(), "Fuente cambio durante copia: " + p); }
    }
    require(inventory(from) == before && inventory(to) == before, "La fuente cambio durante copia; candidato conservado: " + to);
}
void move(const QString &from, const QString &to) {
    ordinary(from); ordinary(to); require(!QFileInfo::exists(to) && QDir().rename(from, to), "No se puede renombrar; conserve journal: " + from + " -> " + to);
}
QString configurationRoot() {
    const auto override = qEnvironmentVariable("ARTEST_SDK_CONFIG_ROOT");
    return absolute(override.isEmpty() ? qEnvironmentVariable("LOCALAPPDATA") + "/ARTest/sdk-authoring" : override);
}
QJsonObject registry(const QString &root) {
    ordinary(root); const auto path = root + "/installations.json";
    if (!QFileInfo::exists(path)) return {{"schema", "artest.schema.sdk-installations.v1"}, {"schemaVersion", 1}, {"profiles", QJsonObject{}}};
    const auto r = load(path);
    require(r.value("schema") == "artest.schema.sdk-installations.v1" && r.value("schemaVersion") == 1 && r.value("profiles").isObject(), "Perfil local incompatible; conserve " + path);
    require(r.value("profiles").toObject().size() <= 64, "Mas de 64 perfiles; seleccion manual requerida"); return r;
}
QJsonObject manualProfile(const QString &path, const QString &root) {
    const auto cli = absolute(QFileInfo(path).isDir() ? path + "/ARTestCLI.exe" : path);
    const auto profiles = registry(root).value("profiles").toObject(); QJsonObject matched;
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        if (absolute(it.value().toObject().value("cli").toString()).compare(cli, Qt::CaseInsensitive) == 0) {
            require(matched.isEmpty(), "Hay varios perfiles para este CLI; seleccione uno de la lista.");
            matched = it.value().toObject(); matched["name"] = it.key();
        }
    }
    if (!matched.isEmpty()) return matched;
    const auto name = "cli-" + hash(cli.toCaseFolded().toUtf8()).left(16);
    return {{"name", name}, {"cli", cli}, {"engine", QFileInfo(cli).absolutePath() + "/ARTestEngine.dll"},
        {"catalog", root + "/i/" + name + "/extensions"}, {"configuration", root + "/i/" + name + "/configuration"}};
}
QList<QJsonObject> candidates(const QString &root) {
    const auto r = registry(root); const auto profiles = r.value("profiles").toObject(); QList<QJsonObject> result;
    const QString selected = r.value("selected").toString();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        auto p = it.value().toObject(); p["name"] = it.key(); if (it.key() == selected) result.prepend(p); else result.append(p);
    }
    auto paths = qEnvironmentVariable("PATH").split(';', Qt::SkipEmptyParts).mid(0, 32);
    paths << qEnvironmentVariable("ProgramFiles") + "/ARTest" << qEnvironmentVariable("LOCALAPPDATA") + "/Programs/ARTest";
    for (const auto &path : paths) {
        if (!QDir::isAbsolutePath(path) || !QFileInfo::exists(path + "/ARTestCLI.exe")) continue;
        bool known = false; for (const auto &existing : result) if (absolute(existing.value("cli").toString()).compare(absolute(path + "/ARTestCLI.exe"), Qt::CaseInsensitive) == 0) known = true;
        if (known) continue;
        const auto p = manualProfile(path, root); bool found = false;
        for (const auto &existing : result) if (existing.value("name") == p.value("name")) found = true;
        if (!found) result.append(p);
    }
    return result;
}
static bool nested(const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) == 0 || a.startsWith(b + '/', Qt::CaseInsensitive); }
void checkProfile(const QJsonObject &p) {
    require(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$").match(p.value("name").toString()).hasMatch(), "Nombre de perfil invalido");
    for (const auto &key : {"cli", "engine", "catalog", "configuration"}) { require(!p.value(key).toString().isEmpty(), "Perfil incompleto"); ordinary(p.value(key).toString()); }
    const auto cli = absolute(p.value("cli").toString()), engine = absolute(p.value("engine").toString());
    require(QFileInfo(cli).fileName().compare("ARTestCLI.exe", Qt::CaseInsensitive) == 0 && QFileInfo(cli).isFile() && QFileInfo(engine).isFile() &&
        engine.compare(QFileInfo(cli).absolutePath() + "/ARTestEngine.dll", Qt::CaseInsensitive) == 0, "Seleccione una instalacion separada con ARTestCLI.exe y ARTestEngine.dll adyacentes");
    const auto catalog = absolute(p.value("catalog").toString()), config = absolute(p.value("configuration").toString());
    require(!nested(catalog, config) && !nested(config, catalog) && QFileInfo(catalog).absolutePath() != catalog && QFileInfo(config).absolutePath() != config,
        "Catalogo y configuracion deben ser carpetas separadas, no anidadas");
    require(!nested(cli, catalog) && !nested(engine, catalog) && !nested(cli, config) && !nested(engine, config), "El runtime debe estar fuera de las salidas de registro");
}
Lock::Lock(const QString &path) {
    ordinary(path); require(QDir().mkpath(QFileInfo(path).absolutePath()), "No se puede crear carpeta de lock");
    handle_ = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(handle_ != INVALID_HANDLE_VALUE, "Destino ocupado o sin permisos; no se intento elevar: " + path);
}
Lock::~Lock() { CloseHandle(handle_); }
static QJsonObject record(const QString &path, const QJsonObject &value) {
    const bool had = QFileInfo::exists(path); const auto old = had ? read(path) : QByteArray{};
    const auto next = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return {{"path", path}, {"hadPrevious", had}, {"previousBase64", QString::fromLatin1(old.toBase64())}, {"previousSha256", had ? hash(old) : QString()},
        {"newBase64", QString::fromLatin1(next.toBase64())}, {"newSha256", hash(next)}};
}
static void checkRecord(const QJsonObject &r, const QString &path) {
    QStringList expected{"path", "hadPrevious", "previousBase64", "previousSha256", "newBase64", "newSha256"}; expected.sort();
    require(r.keys() == expected, "Snapshot con campos desconocidos o ausentes; conservado");
    for (const auto &key : {"path", "previousBase64", "previousSha256", "newBase64", "newSha256"}) require(r.value(key).isString(), "Tipo invalido en snapshot");
    require(r.size() == 6 && r.value("path").isString() && absolute(r.value("path").toString()) == absolute(path) && r.value("hadPrevious").isBool(), "Snapshot ajeno o incompleto");
    ordinary(path);
    for (const auto &prefix : {QString("previous"), QString("new")}) {
        const auto encoded = r.value(prefix + "Base64").toString().toLatin1(); const auto bytes = QByteArray::fromBase64(encoded);
        require(bytes.toBase64() == encoded && (prefix == "previous" && !r.value("hadPrevious").toBool() ? bytes.isEmpty() && r.value(prefix + "Sha256") == "" : hash(bytes) == r.value(prefix + "Sha256")), "Snapshot corrupto; conserve journal");
    }
    if (QFileInfo::exists(path)) { const auto h = hash(read(path)); require(h == r.value("newSha256") || (r.value("hadPrevious").toBool() && h == r.value("previousSha256")), "Archivo cambiado por otro escritor: " + path); }
    else require(!r.value("hadPrevious").toBool(), "Falta archivo previo: " + path);
}
static void putRecord(const QJsonObject &r, bool previous) {
    const auto path = r.value("path").toString(); checkRecord(r, path);
    if (previous && !r.value("hadPrevious").toBool()) {
        if (QFileInfo::exists(path)) require(QFile::remove(path), "No se pudo restaurar ausencia: " + path);
    } else write(path, QByteArray::fromBase64(r.value(previous ? "previousBase64" : "newBase64").toString().toLatin1()));
}
Transaction::Transaction(QJsonObject p, QString root) : profile_(std::move(p)), root_(absolute(root)) {
    checkProfile(profile_); ordinary(root_);
    for (const auto &k : {"cli", "engine", "catalog", "configuration"}) profile_[k] = absolute(profile_.value(k).toString());
    selectionLock_ = std::make_unique<Lock>(root_ + "/.artest-installations.lock");
    const auto config = profile_.value("configuration").toString();
    require(!nested(root_ + "/installations.json", profile_.value("catalog").toString()), "Perfil local dentro del catalogo");
    targetLock_ = std::make_unique<Lock>(QFileInfo(config).absolutePath() + "/." + QFileInfo(config).fileName() + ".artest-register.lock");
    require(QDir().mkpath(config) && QDir().mkpath(QFileInfo(profile_.value("catalog").toString()).absolutePath()), "Destino sin permisos de escritura");
    journalPath_ = config + "/.artest-register-transaction.json";
}
void Transaction::phase(const QString &v) { journal_["phase"] = v; save(journalPath_, journal_); }
QString Transaction::candidate() const { return journal_.value("candidate").toString(); }
QString Transaction::mapping() const { return profile_.value("configuration").toString() + "/.artest-register-scratch." + journal_.value("token").toString() + "/python-environments.json"; }
void Transaction::validateJournal(const QJsonObject &j, bool recovering) const {
    require(j.size() == 13 && j.value("schema") == "artest.schema.sdk-registration-transaction.v1" && j.value("schemaVersion") == 1 && j.value("installation") == profile_.value("name") && j.value("hadCatalog").isBool(), "Journal incompatible o ajeno; conservado");
    const auto token = j.value("token").toString(), phaseValue = j.value("phase").toString();
    require(QRegularExpression("^[0-9a-f]{32}$").match(token).hasMatch() && QStringList{"prepared", "backingUp", "promoting", "writingState", "writingProfile", "committed"}.contains(phaseValue), "Fase/token invalido");
    const auto p = j.value("profile").toObject(); require(p.size() == 5 && absolute(p.value("registry").toString()) == root_ + "/installations.json", "Journal de otro perfil");
    for (const auto &k : {"cli", "engine", "catalog", "configuration"}) require(absolute(p.value(k).toString()) == profile_.value(k), "Journal de otro destino");
    const auto catalog = profile_.value("catalog").toString(), config = profile_.value("configuration").toString();
    const auto prefix = QFileInfo(catalog).absolutePath() + "/." + QFileInfo(catalog).fileName();
    require(absolute(j.value("catalog").toString()) == catalog && absolute(j.value("candidate").toString()) == prefix + ".artest-candidate." + token && absolute(j.value("backup").toString()) == prefix + ".artest-backup." + token, "Rutas ajenas en journal");
    for (const auto &k : {"catalog", "candidate", "backup"}) ordinary(j.value(k).toString());
    for (const auto &k : {"previousCatalogInventory", "candidateCatalogInventory"}) {
        require(j.value(k).isObject(), "Falta inventario"); const auto inv = j.value(k).toObject(); require(inv.size() <= 8192, "Inventario demasiado grande");
        for (auto it = inv.begin(); it != inv.end(); ++it) require(!it.key().isEmpty() && !QDir::isAbsolutePath(it.key()) && !it.key().contains('\\') && !it.key().split('/').contains("..") && QRegularExpression("^[0-9a-f]{64}$").match(it.value().toString()).hasMatch(), "Inventario invalido");
    }
    require(j.value("hadCatalog").toBool() || j.value("previousCatalogInventory").toObject().isEmpty(), "Inventario previo sin ownership");
    const auto files = j.value("files").toObject(); require(files.size() == 3, "Snapshots incompletos");
    checkRecord(files.value("state").toObject(), config + "/registrations.json"); checkRecord(files.value("mapping").toObject(), config + "/python-environments.json"); checkRecord(files.value("profile").toObject(), root_ + "/installations.json");
    const auto c = j.value("candidate").toString(), b = j.value("backup").toString();
    const bool C = QFileInfo::exists(c), B = QFileInfo::exists(b), A = QFileInfo::exists(catalog), had = j.value("hadCatalog").toBool();
    if (C) require(inventory(c) == j.value("candidateCatalogInventory").toObject(), "Candidato alterado; conservado");
    if (B) require(had && inventory(b) == j.value("previousCatalogInventory").toObject(), "Backup alterado; conservado");
    if (recovering) return;
    const bool old = A && had && inventory(catalog) == j.value("previousCatalogInventory").toObject();
    const bool next = A && inventory(catalog) == j.value("candidateCatalogInventory").toObject();
    bool valid = false;
    if (phaseValue == "prepared") valid = C && !B && (had ? old : !A);
    if (phaseValue == "backingUp") valid = C && (had ? ((old && !B) || (!A && B)) : (!A && !B));
    if (phaseValue == "promoting") valid = (C != next) && (had == B) && (!A || next);
    if (phaseValue == "writingState" || phaseValue == "writingProfile") valid = !C && next && (had == B);
    if (phaseValue == "committed") valid = !C && next && (!B || had);
    require(valid, "Topologia ambigua; conserve journal, catalogo y backups");
    if (phaseValue == "committed") for (const auto &key : {"state", "mapping", "profile"}) { const auto r = files.value(key).toObject(); require(hash(read(r.value("path").toString())) == r.value("newSha256"), "Seleccion committed alterada"); }
}
void Transaction::recover() {
    const auto cursorPath = journalPath_ + ".recovery";
    if (!QFileInfo::exists(journalPath_)) {
        if (QFileInfo::exists(cursorPath)) {
            const auto cursor = load(cursorPath);
            require(cursor.size() == 4 && cursor.value("format") == "ARTestDev.RegistrationRecovery.1" && cursor.value("step") == "done" && QRegularExpression("^[0-9a-f]{32}$").match(cursor.value("token").toString()).hasMatch(), "Cursor huerfano; conserve evidencia para inspeccion");
            const auto archived = journalPath_ + ".rolled-back." + cursor.value("token").toString();
            require(hash(read(archived)) == cursor.value("journalSha256"), "Archivo de recuperacion alterado");
            move(cursorPath, cursorPath + ".done." + cursor.value("token").toString());
        }
        return;
    }
    journal_ = load(journalPath_); const auto bytes = read(journalPath_); QJsonObject cursor;
    const auto catalog = journal_.value("catalog").toString(), c = candidate(), backup = journal_.value("backup").toString();
    const auto token = journal_.value("token").toString();
    if (QFileInfo::exists(cursorPath)) {
        cursor = load(cursorPath); require(cursor.size() == 4 && cursor.value("format") == "ARTestDev.RegistrationRecovery.1" && cursor.value("journalSha256") == hash(bytes) && cursor.value("token") == token, "Cursor de recuperacion corrupto; conservado");
        validateJournal(journal_, true);
    } else {
        validateJournal(journal_, false);
        if (journal_.value("phase") == "committed") { move(journalPath_, journalPath_ + ".committed." + token); journal_ = {}; return; }
        const auto ph = journal_.value("phase").toString();
        const bool promoted = ph == "writingState" || ph == "writingProfile" || (ph == "promoting" && QFileInfo::exists(catalog) && !QFileInfo::exists(c));
        cursor = {{"format", "ARTestDev.RegistrationRecovery.1"}, {"journalSha256", hash(bytes)}, {"step", promoted ? "unpromote" : "restore"}, {"token", token}}; save(cursorPath, cursor);
    }
    auto advance = [&](const QString &step) { cursor["step"] = step; save(cursorPath, cursor); };
    const auto previous = journal_.value("previousCatalogInventory").toObject(), next = journal_.value("candidateCatalogInventory").toObject();
    if (cursor.value("step") == "unpromote") {
        if (QFileInfo::exists(catalog)) { require(!QFileInfo::exists(c) && inventory(catalog) == next, "Rollback ambiguo"); move(catalog, c); }
        else require(QFileInfo::exists(c) && inventory(c) == next, "Falta candidato durante rollback");
        advance("restore");
    }
    if (cursor.value("step") == "restore") {
        if (journal_.value("hadCatalog").toBool()) {
            if (QFileInfo::exists(backup)) { require(!QFileInfo::exists(catalog) && inventory(backup) == previous, "Restauracion ambigua"); move(backup, catalog); }
            else require(QFileInfo::exists(catalog) && inventory(catalog) == previous, "No se puede demostrar catalogo anterior");
        } else require(!QFileInfo::exists(catalog), "Catalogo ajeno durante rollback");
        advance("files");
    }
    if (cursor.value("step") == "files" || cursor.value("step") == "done") {
        require(!QFileInfo::exists(backup) && (journal_.value("hadCatalog").toBool() ? QFileInfo::exists(catalog) && inventory(catalog) == previous : !QFileInfo::exists(catalog)), "Topologia de recuperacion alterada; conserve evidencia");
    }
    if (cursor.value("step") == "files") {
        const auto files = journal_.value("files").toObject(); for (const auto &k : {"state", "mapping", "profile"}) putRecord(files.value(k).toObject(), true);
        advance("done");
    }
    require(cursor.value("step") == "done", "Fase de recuperacion desconocida");
    for (const auto &key : {"state", "mapping", "profile"}) {
        const auto record = journal_.value("files").toObject().value(key).toObject();
        const auto path = record.value("path").toString();
        require(record.value("hadPrevious").toBool() ? QFileInfo::exists(path) && hash(read(path)) == record.value("previousSha256") : !QFileInfo::exists(path), "Restauracion de seleccion no confirmada; conserve journal");
    }
    move(journalPath_, journalPath_ + ".rolled-back." + token);
    move(cursorPath, cursorPath + ".done." + token);
    journal_ = {};
}
void Transaction::stage(const QString &package, const QString &receipt, const QString &revision, const QString &extension, const QString &language) {
    require(!QFileInfo::exists(journalPath_), "Recuperacion pendiente");
    const auto catalog = profile_.value("catalog").toString(), config = profile_.value("configuration").toString();
    const auto statePath = config + "/registrations.json", mappingPath = config + "/python-environments.json";
    auto state = QFileInfo::exists(statePath) ? load(statePath) : QJsonObject{{"schema", "artest.schema.sdk-registration-state.v1"}, {"schemaVersion", 1}, {"registrations", QJsonObject{}}};
    require(state.value("schema") == "artest.schema.sdk-registration-state.v1" && state.value("schemaVersion") == 1 && state.value("registrations").isObject(), "Estado de registro incompatible");
    auto registrations = state.value("registrations").toObject(); const auto prior = registrations.value(extension).toObject();
    auto associations = QFileInfo::exists(mappingPath) ? load(mappingPath) : QJsonObject{};
    const auto packageName = "registered-" + hash(extension.toUtf8()).left(16), token = QUuid::createUuid().toString(QUuid::Id128);
    const auto prefix = QFileInfo(catalog).absolutePath() + "/." + QFileInfo(catalog).fileName();
    const auto c = prefix + ".artest-candidate." + token, backup = prefix + ".artest-backup." + token;
    const bool had = QFileInfo::exists(catalog); const auto previous = had ? inventory(catalog) : QJsonObject{};
    if (had) copy(catalog, c); else require(QDir().mkpath(c), "No se puede crear candidato");
    QSet<QString> ids; QString active;
    for (const auto &dir : QDir(c).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const auto path = c + '/' + dir + "/artest-extension.json"; if (!QFileInfo::exists(path)) continue;
        const auto id = load(path).value("extensionId").toString(); require(!id.isEmpty() && !ids.contains(id), "IDs duplicados en catalogo; no se publico"); ids.insert(id); if (id == extension) active = dir;
    }
    if (!active.isEmpty()) {
        require(!prior.isEmpty() && active == packageName && prior.value("packageName") == active && inventory(c + '/' + active) == prior.value("inventory").toObject(), "Colision de identidad u ownership alterado; no se reemplaza el paquete");
        move(c + '/' + active, prefix + ".artest-retired-package." + token);
    } else require(prior.isEmpty(), "Estado sin paquete activo; requiere inspeccion");
    require(!QFileInfo::exists(c + '/' + packageName), "Nombre de carpeta ocupado por archivos ajenos"); copy(package, c + '/' + packageName);
    const auto manifest = load(package + "/artest-extension.json"); require(manifest.value("extensionId") == extension, "Identidad de paquete distinta del proyecto");
    if (language == "python") associations[extension] = receipt; else associations.remove(extension);
    if (prior.value("revisionId") != revision || prior.value("inventory").toObject() != inventory(package)) registrations[extension] = QJsonObject{{"language", language}, {"packageName", packageName}, {"revisionId", revision}, {"version", manifest.value("version")}, {"package", package}, {"receipt", receipt.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(receipt)}, {"inventory", inventory(package)}, {"registeredAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    state["registrations"] = registrations;
    auto selection = registry(root_); auto profiles = selection.value("profiles").toObject(); auto profile = profile_; const auto name = profile.take("name"); profiles[name.toString()] = profile; selection["profiles"] = profiles; selection["selected"] = name;
    profile["registry"] = root_ + "/installations.json";
    journal_ = {{"schema", "artest.schema.sdk-registration-transaction.v1"}, {"schemaVersion", 1}, {"installation", name}, {"profile", profile}, {"token", token}, {"phase", "prepared"}, {"catalog", catalog}, {"candidate", c}, {"backup", backup}, {"hadCatalog", had}, {"previousCatalogInventory", previous}, {"candidateCatalogInventory", inventory(c)},
        {"files", QJsonObject{{"state", record(statePath, state)}, {"mapping", record(mappingPath, associations)}, {"profile", record(root_ + "/installations.json", selection)}}}};
    // Journal has the same 13 fields as registration.ps1; public data is unchanged.
    save(journalPath_, journal_); save(mapping(), associations);
}
void Transaction::promote() {
    validateJournal(journal_, false);
    phase("backingUp"); if (journal_.value("hadCatalog").toBool()) move(journal_.value("catalog").toString(), journal_.value("backup").toString());
    phase("promoting"); move(candidate(), journal_.value("catalog").toString());
    phase("writingState"); const auto files = journal_.value("files").toObject(); putRecord(files.value("state").toObject(), false); putRecord(files.value("mapping").toObject(), false);
}
QString Transaction::commit() {
    validateJournal(journal_, false); phase("writingProfile"); putRecord(journal_.value("files").toObject().value("profile").toObject(), false);
    phase("committed");
    try { recover(); }
    catch (const std::exception &e) { return "Publicacion committed; archivo de evidencia pendiente: " + QString::fromUtf8(e.what()); }
    return {};
}
}

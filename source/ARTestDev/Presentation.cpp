#include "Presentation.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ARTestDev {
namespace {
void require(bool condition, const QString &message) {
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
}
void ordinary(QString path) {
    for (;;) {
        const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
        require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "Ruta ausente/insegura: " + path);
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path) return;
        path = parent;
    }
}
struct Reader {
    qint64 budget = 128LL * 1024 * 1024;
    int entries = 0;
    QSet<QString> paths(const QString &root, const QString &directory, const QSet<QString> &ignored = {}, int depth = 0) {
        require(depth < 32 && ++entries <= 8192, "Árbol excesivo; cobertura incompleta");
        ordinary(directory);
        QSet<QString> out;
        for (const auto &entry : QDir(directory).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
            ordinary(entry.absoluteFilePath());
            if (ignored.contains(entry.fileName().toLower()) && (depth == 0 || entry.isDir())) continue;
            const auto relative = QDir(root).relativeFilePath(entry.absoluteFilePath());
            if (entry.isDir()) out.unite(paths(root, entry.absoluteFilePath(), ignored, depth + 1));
            else { require(entry.isFile() && ++entries <= 8192, "Archivo no ordinario/árbol excesivo"); out.insert(relative); }
        }
        return out;
    }
    QByteArray bytes(const QString &path, qint64 maximum = 2 * 1024 * 1024) {
        ordinary(path);
        QFile file(path);
        require(++entries <= 8192 && file.open(QIODevice::ReadOnly) && file.size() <= maximum && file.size() <= budget,
                "Lectura incompleta: límite de inventario/tamaño o acceso en " + path);
        budget -= file.size();
        return file.readAll();
    }
    QJsonObject object(const QString &path) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes(path), &error);
        require(error.error == QJsonParseError::NoError && document.isObject() && !document.object().isEmpty(), "Metadata/configuración corrupta: " + path);
        return document.object();
    }
    QString hash(const QString &path) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes(path, 64 * 1024 * 1024), QCryptographicHash::Sha256).toHex());
    }
    QString contained(const QString &root, const QString &relative) {
        require(!relative.isEmpty() && !relative.contains('\\') && !relative.contains(':') && !QDir::isAbsolutePath(relative), "Ruta relativa inválida");
        const auto path = QDir::cleanPath(root + '/' + relative);
        require(path.startsWith(root + '/', Qt::CaseInsensitive), "Ruta fuera del proyecto");
        ordinary(path);
        return path;
    }
    void verify(const QString &root, const QJsonObject &inventory, bool exact = false, const QSet<QString> &ignored = {}) {
        require(!inventory.isEmpty() && inventory.size() <= 4096, "Inventario ausente/excesivo");
        for (auto it = inventory.begin(); it != inventory.end(); ++it) {
            const QString path = contained(root, it.key());
            if (it.value() == "directory") require(QFileInfo(path).isDir(), "Directorio de inventario ausente");
            else require(hash(path) == it.value().toString(), "Metadata obsoleta/inventario alterado: " + path);
        }
        if (exact) {
            QSet<QString> expected;
            for (auto it = inventory.begin(); it != inventory.end(); ++it) if (it.value() != "directory") expected.insert(it.key());
            require(paths(root, root, ignored) == expected, "Archivos adicionales/ausentes; metadata no vigente: " + root);
        }
    }
    QJsonObject records(const QJsonArray &array) {
        QJsonObject out;
        for (const auto &value : array) {
            const auto row = value.toObject(); const QString path = row.value("path").toString();
            require(!path.isEmpty() && !out.contains(path), "Inventario ambiguo");
            out.insert(path, row.value("sha256"));
        }
        return out;
    }
};
Presentation inspect(const QString &root, Reader &reader) {
    Presentation out;
    try {
        const auto config = reader.object(root + "/artest-sdk-project.json");
        require(config.value("schema") == "artest.schema.sdk-authoring-project.v1" && config.value("schemaVersion") == 1, "Proyecto incompatible");
        QJsonObject manifest;
        if (config.value("language") == "cpp") {
            // Prefer a single current configuration. Conflicting configurations are
            // ambiguous, never resolved by timestamps or directory order.
            for (const auto &configuration : {QString("Debug"), QString("Release")}) {
                const QString state = root + "/.artest/native/" + configuration;
                if (!QFileInfo::exists(state + "/current")) continue;
                require(!QFileInfo::exists(state + "/transaction.json"), "Build/recuperación pendiente");
                const QString current = state + "/current";
                const auto owner = reader.object(current + "/ownership.json");
                require(owner.value("format") == "ARTestDev.NativeOutput.1" &&
                    QDir::fromNativeSeparators(owner.value("project").toString()).compare(root + '/' + config.value("projectFile").toString(), Qt::CaseInsensitive) == 0,
                    "Ownership no corresponde al proyecto; copie fuentes y reconstruya");
                reader.verify(current, owner.value("files").toObject(), true, {"ownership.json"});
                const auto inputs = reader.object(current + "/inputs.json");
                const auto sources = inputs.value("sources").toObject();
                reader.verify(root, sources);
                const auto actualSources = reader.paths(root, root, {"bin", "obj", "out", ".artest", ".vs", ".git"});
                for (const auto &path : actualSources)
                    require(sources.contains(path) || QStringList{"json", "md", "txt"}.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive),
                            "Fuentes nuevas sin Build: " + path);
                const auto sdk = QDir::cleanPath(QDir::fromNativeSeparators(inputs.value("request").toObject().value("sdk").toString()) + "/..");
                require(!QDir(sdk).isRoot() && QFileInfo::exists(sdk + "/artestdev-staging.json"), "SDK de autoría no verificable");
                reader.verify(sdk, inputs.value("sdk").toObject(), true);
                const auto candidate = reader.object(current + "/package/artest-extension.json");
                require(manifest.isEmpty() || manifest.value("components") == candidate.value("components"), "Configuraciones con metadata distinta; cobertura incompleta");
                manifest = candidate;
            }
        } else if (config.value("language") == "python") {
            const auto portable = reader.object(root + "/artest-project.json");
            const QString state = root + "/.artest/stage3";
            const auto ready = reader.object(state + "/ready.json");
            const QString identity = ready.value("preparationId").toString();
            require(ready.value("schemaVersion") == 1 && QRegularExpression("^[a-f0-9]{64}$").match(identity).hasMatch() &&
                    ready.value("revision") == "revisions/" + identity && ready.value("association") == "revisions/" + identity + "/python-environments.json", "Selección Python inválida");
            const QString revision = reader.contained(state, ready.value("revision").toString());
            const auto preparation = reader.object(revision + "/preparation.json");
            require(preparation.value("schemaVersion") == 1 && preparation.value("preparationId") == identity, "Identidad de preparación inválida");
            const auto inputs = preparation.value("inputs").toObject();
            reader.verify(reader.contained(root, portable.value("sourceDirectory").toString()), reader.records(inputs.value("source").toArray()), true, {"__pycache__", ".venv", ".git"});
            require(inputs.value("entryPoint") == portable.value("entryPoint") &&
                reader.hash(reader.contained(root, portable.value("dependencyLock").toString())) == inputs.value("dependencyLockSha256"), "Preparación obsoleta");
            manifest = reader.object(revision + "/package/artest-extension.json");
            reader.verify(revision + "/package", reader.records(manifest.value("inventory").toArray()), true, {"artest-extension.json"});
            const auto receipt = reader.object(revision + "/environment/artest-environment.json");
            require(receipt.value("schemaVersion") == 1 && receipt.value("extensionId") == config.value("extensionId") &&
                    receipt.value("sdkSha256") == inputs.value("sdkWheelSha256"), "Recibo de preparación inconsistente");
            require(reader.hash(revision + "/package/artest-extension.json") == receipt.value("packageSha256"), "Manifest Python alterado");
        }
        require(!manifest.isEmpty() && manifest.value("extensionId") == config.value("extensionId") && manifest.value("components").isArray(), "Metadata ausente o de otra extensión");
        const auto components = manifest.value("components").toArray();
        require(!components.isEmpty() && components.size() <= 256, "Metadata de componentes ausente/excesiva");
        QSet<QString> ids;
        for (const auto &value : components) {
            const auto row = value.toObject();
            const auto id = row.value("typeId").toString();
            require(row.value("displayName").isString() && row.value("displayName").toString().size() <= 1024,
                    "Nombre de componente ausente/excesivo para comparación local");
            require(!id.isEmpty() && !ids.contains(id), "ID duplicado en metadata: " + id);
            ids.insert(id);
        }
        out.components = components;
        out.current = true;
    } catch (const std::exception &error) {
        out.diagnostics << "Cobertura incompleta en " + root + ": " + QString::fromUtf8(error.what()) + ". Build/preparación explícita requerida; no se ejecutó código.";
    }
    return out;
}
}
QString comparisonName(const QString &name) {
    QString result;
    for (const char32_t scalar : name.toCaseFolded().normalized(QString::NormalizationForm_D).toUcs4())
        if (QChar::isLetterOrNumber(scalar)) result += QString::fromUcs4(&scalar, 1);
    return result;
}
QString nameSimilarity(const QString &left, const QString &right) {
    const auto a = comparisonName(left).toUcs4(), b = comparisonName(right).toUcs4();
    if (a.isEmpty() || b.isEmpty()) return {};
    if (a == b) return "nombres equivalentes";
    if (a.size() < 6 || b.size() < 6 || qAbs(a.size() - b.size()) > 1) return {};
    qsizetype i = 0, j = 0; int edits = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { ++i; ++j; continue; }
        if (++edits > 1) return {};
        if (a.size() >= b.size()) ++i;
        if (b.size() >= a.size()) ++j;
    }
    edits += static_cast<int>(a.size() - i + b.size() - j);
    return edits <= 1 ? "nombres parecidos (distancia <= 1)" : QString();
}
Presentation inspectPresentation(const QString &project) {
    Reader reader;
    return inspect(QDir::cleanPath(QDir::fromNativeSeparators(project)), reader);
}
QStringList localNameWarnings(const QString &workspace, const QString &self, const QJsonArray &components, const QString &extensionId) {
    QStringList warnings;
    Reader reader;
    int comparisons = 0;
    const auto compare = [&](const QJsonArray &other, const QString &project, bool same) {
        for (qsizetype i = 0; i < components.size(); ++i) for (qsizetype j = same ? i + 1 : 0; j < other.size(); ++j) {
            require(++comparisons <= 65536 && warnings.size() < 100, "Comparación local incompleta: límite de resultados/comparaciones");
            const auto a = components[i].toObject(), b = other[j].toObject();
            if (a.value("typeId") == b.value("typeId")) {
                warnings << "ERROR ID local duplicado: " + a.value("typeId").toString() + " en " + project;
            } else if (a.value("kind") == b.value("kind")) {
                const auto reason = nameSimilarity(a.value("displayName").toString(), b.value("displayName").toString());
                if (!reason.isEmpty()) warnings << "WARNING " + a.value("kind").toString() + ": " + a.value("displayName").toString() + " / " + b.value("displayName").toString() + " — " + project + ": " + reason;
            }
        }
    };
    try {
        compare(components, self, true);
        ordinary(workspace);
        const auto directories = QDir(workspace).entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
        require(directories.size() <= 1024, "Workspace supera 1024 subdirectorios; cobertura incompleta");
        for (const auto &directory : directories) {
            const auto path = directory.absoluteFilePath();
            if (path.compare(self, Qt::CaseInsensitive) == 0) continue;
            if (!QFileInfo::exists(path + "/artest-sdk-project.json")) continue;
            const auto known = reader.object(path + "/artest-sdk-project.json");
            if (!extensionId.isEmpty() && known.value("extensionId") == extensionId)
                warnings << "ERROR ID local duplicado: extensión " + extensionId + " en " + path + ". Una copia conserva la identidad de la misma extensión.";
            const auto observed = inspect(path, reader);
            warnings.append(observed.diagnostics);
            if (observed.current) compare(observed.components, path, false);
            if (reader.budget <= 0 || warnings.size() >= 100) { warnings << "Cobertura incompleta: límite local alcanzado."; break; }
        }
    } catch (const std::exception &error) { warnings << "Cobertura incompleta: " + QString::fromUtf8(error.what()); }
    return warnings;
}
}

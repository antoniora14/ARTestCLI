#include "SdkLocation.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ARTestDev {
namespace {
bool ordinary(QString path) {
    if (!QDir::isAbsolutePath(path) || path.startsWith("//")) return false;
    for (;;) {
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path) return true;
        path = parent;
    }
}
bool relativeFile(const QString &path) {
    if (path.isEmpty() || path.contains(QRegularExpression(QStringLiteral("[\\\\:<>\"|?*\\x00-\\x1f]"))) || path.startsWith('/')) return false;
    for (const QString &part : path.split('/'))
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')) return false;
    return true;
}
bool scan(const QString &root, const QString &directory, QSet<QString> &files, QStringList &errors) {
    if (!ordinary(directory)) { errors << "Reparse point o directorio inaccesible: " + directory; return false; }
    const QDir dir(directory);
    if (!dir.isReadable()) { errors << "Directorio ilegible: " + directory; return false; }
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        if (!ordinary(info.absoluteFilePath())) { errors << "Reparse point o archivo inaccesible: " + info.absoluteFilePath(); continue; }
        if (info.isDir()) scan(root, info.absoluteFilePath(), files, errors);
        else if (info.isFile()) files.insert(QDir(root).relativeFilePath(info.absoluteFilePath()).toLower());
        else errors << "Entrada no ordinaria: " + info.absoluteFilePath();
    }
    return errors.isEmpty();
}
}
Kit installedSdk() { return inspectStagingExecutable(QCoreApplication::applicationFilePath()); }

Kit inspectStagingExecutable(const QString &executable) {
    Kit out;
    out.origin = Kit::Origin::DevelopmentStaging;
    out.root = QDir::cleanPath(QFileInfo(executable).absolutePath());
    const auto failure = [&out]() {
        out.diagnostics << QStringLiteral("Instalación ARTestDev incompleta, corrupta o incompatible en %1. Repare o reinstale sus recursos; en desarrollo vuelva a ensamblar desde fuentes verificadas en una carpeta nueva. No se seleccionará otro SDK.").arg(out.root);
        return out;
    };
    if (!QDir::isAbsolutePath(executable) || !ordinary(executable) || QFileInfo(executable).fileName().compare("ARTestDev.exe", Qt::CaseInsensitive) != 0) {
        out.diagnostics << "Ejecutable ausente, inesperado o con reparse points: " + executable;
        return failure();
    }
    const QString descriptor = out.root + "/artestdev-staging.json";
    QFile file(descriptor);
    if (!ordinary(descriptor) || !file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        out.diagnostics << "Descriptor privado ausente, ilegible o inseguro: " + descriptor;
        return failure();
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto manifest = document.object();
    const QString configuration = manifest.value("configuration").toString();
    const QJsonObject versions{{"nativeSdk", "0.4.0"}, {"pythonSdk", "0.2.0"}, {"qt", "6.8.3"}};
    const QJsonObject paths{{"executable", "ARTestDev.exe"}, {"nativeSdk", "native-sdk"}, {"projectTool", "python/tools/project.py"}, {"pythonTemplate", "python/templates/minimal"}};
    if (parse.error != QJsonParseError::NoError || !document.isObject() ||
        manifest.value("internalVersion").toDouble() != 1 || manifest.value("platform") != "windows-x64" ||
        (configuration != "Debug" && configuration != "Release") ||
        manifest.value("components").toObject() != versions || manifest.value("paths").toObject() != paths) {
        out.diagnostics << "Descriptor privado o versiones de componentes incompatibles.";
        return failure();
    }
    QSet<QString> expected;
    if (!manifest.value("files").isArray() || manifest.value("files").toArray().isEmpty()) out.diagnostics << "Inventario ausente o vacío.";
    for (const QJsonValue &value : manifest.value("files").toArray()) {
        const auto record = value.toObject();
        const QString path = record.value("path").toString();
        const QString hash = record.value("sha256").toString();
        const QString key = path.toLower();
        if (!relativeFile(path) || key == "artestdev-staging.json" || expected.contains(key) ||
            !QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch()) {
            out.diagnostics << "Entrada de inventario inválida, duplicada o con escape: " + path;
            continue;
        }
        expected.insert(key);
        const QString base = QFileInfo(path).fileName().toLower();
        if (base.startsWith("artestcli") || base.startsWith("artestengine") || base.startsWith("artestsdkvalidate") ||
            base == "python.exe" || base == "pythonw.exe" || base.startsWith("python3") || key.contains("/.venv/") || key.contains("/site-packages/"))
            out.diagnostics << "Runtime o herramienta prohibida en staging: " + path;
        QFile input(out.root + '/' + path);
        QCryptographicHash digest(QCryptographicHash::Sha256);
        if (!ordinary(input.fileName()) || !QFileInfo(input.fileName()).isFile() || !input.open(QIODevice::ReadOnly) ||
            !digest.addData(&input) || QString::fromLatin1(digest.result().toHex()) != hash)
            out.diagnostics << "Archivo ausente, corrupto o inseguro: " + path;
    }
    const QString suffix = configuration == "Debug" ? "d" : "";
    QStringList required{"artestdev.exe", "native-sdk/sdk-version.json", "native-sdk/templates/artestextension/artestextensionstarter.vcxproj",
        "native-sdk/templates/artestextension/extension.cpp", "native-sdk/templates/artestextension/readvaluecommand.h",
        "native-sdk/templates/artestextension/simulatedvaluesource.h", "native-sdk/templates/artestextension/.gitignore",
        "native-sdk/templates/artestextension/multipleinstruments.json", "python/tools/project.py",
        "python/templates/minimal/artest-project.json", "python/templates/minimal/src/extension.py",
        "python/templates/minimal/requirements.lock", "python/templates/minimal/plan/measurement.json",
        "python/templates/minimal/artest-project.local.example.json", "python/templates/minimal/.gitignore", "python/tools/package.py",
        "native-sdk/templates/artestextension/testplan.json", "native-sdk/templates/artestextension/readme.md",
        "native-sdk/build/native/artestsdk.props", "native-sdk/third_party_notices.md",
        "native-sdk/include/artestextensionabi.h", "native-sdk/include/nlohmann/json.hpp",
        "notices/qtbase-6.8.3.spdx", "notices/staging-notices.md",
        "python/tools/prepare.py", "python/artest_sdk/__init__.py", "python/artest_sdk/api.py", "python/artest_sdk/schema.py",
        "python/wheels/artest-offline-wheelhouse.json", "python/wheels/artest_python-0.2.0-py3-none-any.whl",
        "python/wheels/protobuf-6.33.4-cp310-abi3-win_amd64.whl", "python/wheels/pywin32-311-cp313-cp313-win_amd64.whl"};
    for (const QString &header : QStringList{"command.h", "context.h", "definition.h", "extension.h", "instrumentdriver.h",
         "metadata.h", "metadatagenerator.h", "parameters.h", "result.h", "schema.h", "testing.h",
         "detail/marshalling.h", "detail/nativeadapter.h", "detail/nativecontext.h"})
        required << "native-sdk/include/artest/" + header;
    for (const QString &module : {QStringLiteral("core"), QStringLiteral("gui"), QStringLiteral("widgets"), QStringLiteral("concurrent")}) required << "qt6" + module + suffix + ".dll";
    required << "platforms/qwindows" + suffix + ".dll";
    if (manifest.contains("nativeBuildProfile")) {
        if (manifest.value("nativeBuildProfile").toInt() != 1) out.diagnostics << "Unsupported native build profile";
        required << "artestdevnative.exe" << "native-sdk/build/native/artestdevnative.targets";
    }
    for (const auto &path : required) if (!expected.contains(path)) out.diagnostics << "Recurso obligatorio fuera del inventario: " + path;
    if (expected.size() != required.size()) out.diagnostics << "El inventario no coincide con los recursos del staging privado versión 1.";
    QSet<QString> actual;
    scan(out.root, out.root, actual, out.diagnostics);
    actual.remove("artestdev-staging.json");
    if (actual != expected) out.diagnostics << "El árbol contiene archivos adicionales o faltantes respecto al inventario.";
    QFile nativeVersion(out.root + "/native-sdk/sdk-version.json");
    if (nativeVersion.open(QIODevice::ReadOnly)) {
        const auto version = QJsonDocument::fromJson(nativeVersion.readAll()).object();
        if (version.value("sdkVersion") != "0.4.0" || version.value("engineApi") != "0.4" ||
            version.value("nativeExtensionAbi") != "0.2" || version.value("platform") != "windows-x64")
            out.diagnostics << "Versión del SDK nativo incompatible con el descriptor privado.";
    }
    if (!out.diagnostics.isEmpty()) return failure();
    out.version = "development-staging-1";
    out.nativeSdk = out.root + "/native-sdk";
    out.projectTool = out.root + "/python/tools/project.py";
    out.valid = true;
    return out;
}
}

#include "Inspection.h"
#include "SdkLocation.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace ARTestDev {
namespace {
QJsonObject readObject(const QString &file, QStringList &errors) {
    QFile input(file);
    if (QFileInfo(file).isSymLink() || QFileInfo(file).isJunction() || !input.open(QIODevice::ReadOnly)) {
        errors << QStringLiteral("No se puede leer %1. Seleccione un archivo ordinario existente.").arg(file);
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(input.read(2 * 1024 * 1024 + 1), &parse);
    if (input.size() > 2 * 1024 * 1024 || parse.error != QJsonParseError::NoError || !doc.isObject()) {
        errors << QStringLiteral("JSON inválido en %1. Corrija el archivo de configuración.").arg(file);
        return {};
    }
    if (doc.object().isEmpty()) errors << QStringLiteral("Configuración vacía en %1. Restaure el archivo válido.").arg(file);
    return doc.object();
}

QString requiredString(const QJsonObject &value, const QString &key, QStringList &errors) {
    const QJsonValue item = value.value(key);
    if (!item.isString() || item.toString().trimmed().isEmpty()) {
        errors << QStringLiteral("Falta %1. Corrija la configuración portable.").arg(key);
        return {};
    }
    return item.toString();
}

bool versionOne(const QJsonObject &value, QStringList &errors) {
    if (!value.value(QStringLiteral("schemaVersion")).isDouble() ||
        value.value(QStringLiteral("schemaVersion")).toDouble() != 1) {
        errors << QStringLiteral("schemaVersion debe ser 1. Use un proyecto compatible.");
        return false;
    }
    return true;
}

QString containedPath(const QString &root, QString relative, QStringList &errors,
                      const QString &field, bool directory, bool projectPath = false) {
    // Project paths follow Windows project.py semantics; kit inventory stays strict.
    if (projectPath) relative = QDir::fromNativeSeparators(relative);
    if (relative.isEmpty() || relative.contains('\\') || relative.contains(':') || relative.contains(QStringLiteral("//")) ||
        QDir::isAbsolutePath(relative) || relative.startsWith('/')) {
        errors << QStringLiteral("%1 debe ser relativo y permanecer en el proyecto/kit.").arg(field);
        return {};
    }
    QStringList parts;
    QString current = root;
    for (const QString &part : relative.split('/')) {
        if (part == QStringLiteral(".")) continue;
        if (part == QStringLiteral("..")) {
            if (parts.isEmpty()) {
                errors << QStringLiteral("%1 debe ser relativo y permanecer en el proyecto/kit.").arg(field);
                return {};
            }
            parts.removeLast();
            current = QDir(root).filePath(parts.join('/'));
            continue;
        }
        parts << part;
        current = QDir(current).filePath(part);
        const QFileInfo partInfo(current);
        if (partInfo.isSymLink() || partInfo.isJunction()) {
            errors << QStringLiteral("%1 contiene un reparse point: %2. Use archivos ordinarios.").arg(field, relative);
            return {};
        }
    }
    if (parts.isEmpty() && !(projectPath && directory)) {
        errors << QStringLiteral("%1 debe nombrar un archivo o directorio bajo el proyecto/kit.").arg(field);
        return {};
    }
    const QFileInfo info(current);
    const QString canonical = QDir::fromNativeSeparators(info.canonicalFilePath());
    const QString prefix = QDir::cleanPath(root) + '/';
    const bool projectRoot = projectPath && directory && canonical.compare(root, Qt::CaseInsensitive) == 0;
    if (canonical.isEmpty() || (!projectRoot && !canonical.startsWith(prefix, Qt::CaseInsensitive)) ||
        (directory ? !info.isDir() : !info.isFile())) {
        errors << QStringLiteral("%1 falta o sale del directorio: %2. Restaure el archivo original.").arg(field, relative);
        return {};
    }
    return canonical;
}

QString containedFile(const QString &root, const QString &relative, QStringList &errors, const QString &field,
                      bool projectPath = false) {
    return containedPath(root, relative, errors, field, false, projectPath);
}

QString localPath(const QString &root, const QJsonObject &value, const QString &key, QStringList &errors) {
    const QJsonValue item = value.value(key);
    if (!item.isString() || item.toString().trimmed().isEmpty()) {
        errors << QStringLiteral("Falta %1. Corrija la configuración local de este equipo.").arg(key);
        return {};
    }
    const QString path = item.toString();
    const QString native = QDir::fromNativeSeparators(path);
    return QDir::cleanPath(QDir::isAbsolutePath(native) ? native : QDir(root).absoluteFilePath(native));
}
}

Project inspectProject(const QString &folder) {
    Project out;
    const QFileInfo rootInfo(folder);
    out.root = QDir::fromNativeSeparators(rootInfo.canonicalFilePath());
    if (!rootInfo.isDir() || rootInfo.isSymLink() || rootInfo.isJunction() || out.root.isEmpty()) {
        out.diagnostics << QStringLiteral("Proyecto inexistente. Seleccione una carpeta de proyecto existente.");
        return out;
    }
    const QString guidedFile = QDir(out.root).filePath(QStringLiteral("artest-sdk-project.json"));
    if (!QFileInfo::exists(guidedFile)) {
        out.diagnostics << QStringLiteral("Esta carpeta no es un proyecto ARTest SDK: falta artest-sdk-project.json. Seleccione la carpeta raíz de un proyecto existente creado con el kit.");
        return out;
    }
    const QJsonObject guided = readObject(guidedFile, out.diagnostics);
    if (guided.isEmpty()) return out;
    versionOne(guided, out.diagnostics);
    if (guided.value(QStringLiteral("schema")).toString() != QStringLiteral("artest.schema.sdk-authoring-project.v1"))
        out.diagnostics << QStringLiteral("Esquema de proyecto incompatible. Abra un proyecto generado por el kit actual.");
    out.name = requiredString(guided, QStringLiteral("name"), out.diagnostics);
    out.language = requiredString(guided, QStringLiteral("language"), out.diagnostics);
    out.variant = requiredString(guided, QStringLiteral("variant"), out.diagnostics);
    out.extensionId = requiredString(guided, QStringLiteral("extensionId"), out.diagnostics);
    if (out.language != QStringLiteral("python") && out.language != QStringLiteral("cpp"))
        out.diagnostics << QStringLiteral("Lenguaje incompatible. Elija un proyecto Python o C++.");
    if (out.variant != QStringLiteral("driver-command") && out.variant != QStringLiteral("driver-only") &&
        out.variant != QStringLiteral("command-only"))
        out.diagnostics << QStringLiteral("Variante incompatible. Use driver-command, driver-only o command-only.");
    out.plan = containedFile(out.root, requiredString(guided, QStringLiteral("plan"), out.diagnostics),
                             out.diagnostics, QStringLiteral("plan"), true);
    if (out.language == QStringLiteral("cpp")) {
        out.projectFile = containedFile(out.root, requiredString(guided, QStringLiteral("projectFile"), out.diagnostics),
                                        out.diagnostics, QStringLiteral("projectFile"), true);
        const QString localFile = QDir(out.root).filePath(QStringLiteral("artest-sdk-project.local.json"));
        if (QFileInfo::exists(localFile)) {
            const QJsonObject local = readObject(localFile, out.prerequisites);
            versionOne(local, out.prerequisites);
            out.msbuild = localPath(out.root, local, QStringLiteral("msbuild"), out.prerequisites);
            if (!QFileInfo(out.msbuild).isExecutable())
                out.prerequisites << QStringLiteral("MSBuild no existe en la ruta local. Instale Visual Studio con MSBuild, C++ x64 y Windows SDK; después corrija msbuild en la configuración local.");
        } else {
            out.prerequisites << QStringLiteral("Falta la configuración local C++. Indique MSBuild y el SDK nativo instalados en este equipo.");
        }
        const QString propsPath = QDir(out.root).filePath(QStringLiteral("ARTestSDK.local.props"));
        QFile props(propsPath);
        if (QFileInfo(propsPath).isSymLink() || QFileInfo(propsPath).isJunction() || !props.open(QIODevice::ReadOnly)) {
            out.prerequisites << QStringLiteral("Falta ARTestSDK.local.props ordinario. Configure la ruta local del SDK nativo.");
        } else {
            QXmlStreamReader xml(&props);
            QString sdkRoot;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QStringLiteral("ARTestSDKRoot"))
                    sdkRoot = xml.readElementText();
            }
            const QString native = QDir::fromNativeSeparators(sdkRoot);
            const QString resolvedSdk = QDir::cleanPath(QDir::isAbsolutePath(native) ? native : QDir(out.root).absoluteFilePath(native));
            const auto staging = inspectStagingExecutable(QDir(resolvedSdk).filePath(QStringLiteral("../ARTestDev.exe")));
            const bool stagingSdk = staging.valid && QDir::cleanPath(resolvedSdk).compare(staging.nativeSdk, Qt::CaseInsensitive) == 0;
            if (xml.hasError() || sdkRoot.isEmpty() || (!stagingSdk && !QFileInfo(QDir(resolvedSdk).filePath(QStringLiteral("sdk-manifest.json"))).isFile()))
                out.prerequisites << QStringLiteral("SDK nativo local ausente o props inválido. Corrija ARTestSDKRoot en ARTestSDK.local.props.");
        }
    } else if (out.language == QStringLiteral("python")) {
        const QJsonObject portable = readObject(QDir(out.root).filePath(QStringLiteral("artest-project.json")), out.diagnostics);
        versionOne(portable, out.diagnostics);
        for (const QString &key : {QStringLiteral("dependencyLock"), QStringLiteral("plan")})
            containedFile(out.root, requiredString(portable, key, out.diagnostics), out.diagnostics, key, true);
        const QString source = requiredString(portable, QStringLiteral("sourceDirectory"), out.diagnostics);
        containedPath(out.root, source, out.diagnostics, QStringLiteral("sourceDirectory"), true, true);
        requiredString(portable, QStringLiteral("entryPoint"), out.diagnostics);
        const QString localFile = QDir(out.root).filePath(QStringLiteral("artest-project.local.json"));
        if (QFileInfo::exists(localFile)) {
            const QJsonObject local = readObject(localFile, out.prerequisites);
            versionOne(local, out.prerequisites);
            out.python = localPath(out.root, local, QStringLiteral("python"), out.prerequisites);
            for (const QString &key : {QStringLiteral("python"), QStringLiteral("sdkWheel")}) {
                const QString path = key == QStringLiteral("python") ? out.python : localPath(out.root, local, key, out.prerequisites);
                if (!QFileInfo(path).isFile()) out.prerequisites << QStringLiteral("Falta %1: %2. Instale o seleccione el archivo local compatible.").arg(key, path);
            }
            const QString cli = localPath(out.root, local, QStringLiteral("cliExecutable"), out.targetDiagnostics);
            if (!QFileInfo(cli).isFile())
                out.targetDiagnostics << QStringLiteral("ARTestCLI no está disponible: %1. Seleccione una instalación local compatible para operaciones que la requieran.").arg(cli);
        } else {
            out.prerequisites << QStringLiteral("Falta artest-project.local.json. Configure Python y el wheel SDK locales para este equipo.");
        }
    }
    out.valid = out.diagnostics.isEmpty();
    return out;
}

Kit inspectKit(const QString &folder) {
    Kit out;
    const QFileInfo rootInfo(folder);
    out.root = QDir::fromNativeSeparators(rootInfo.canonicalFilePath());
    if (!rootInfo.isDir() || rootInfo.isSymLink() || rootInfo.isJunction() || out.root.isEmpty()) {
        out.diagnostics << QStringLiteral("Kit inexistente. Seleccione el directorio extraído del kit.");
        return out;
    }
    const QString manifestFile = QDir(out.root).filePath(QStringLiteral("sdk-manifest.json"));
    if (!QFileInfo::exists(manifestFile)) {
        out.partial = true;
        out.diagnostics << QStringLiteral("Formato de SDK sin manifiesto soportado. DEV-01.7 definirá el nuevo formato sin ARTestCLI, Engine ni Python runtime; validación incompleta.");
        return out;
    }
    const QJsonObject manifest = readObject(manifestFile, out.diagnostics);
    if (manifest.isEmpty()) return out;
    if (manifest.value(QStringLiteral("schema")).toString() != QStringLiteral("artest.schema.development-kit-package.v1")) {
        out.partial = true;
        out.diagnostics << QStringLiteral("Formato de SDK no soportado por DEV-01.1. No se valida su integridad completa; espere la declaración de DEV-01.7.");
        return out;
    }
    if (manifest.value(QStringLiteral("platform")).toString() != QStringLiteral("windows-x64") ||
        manifest.value(QStringLiteral("kitVersion")).toString() != QStringLiteral("0.4.0") ||
        manifest.value(QStringLiteral("stability")).toString() != QStringLiteral("evaluation"))
        out.diagnostics << QStringLiteral("Kit incompatible. Use el kit de desarrollo Windows x64.");
    out.version = requiredString(manifest, QStringLiteral("kitVersion"), out.diagnostics);
    const QJsonValue entries = manifest.value(QStringLiteral("files"));
    if (!entries.isArray()) out.diagnostics << QStringLiteral("Inventario inválido. Extraiga otra copia íntegra del kit.");
    QSet<QString> expected;
    for (const QJsonValue &entry : entries.toArray()) {
        if (!entry.isObject()) { out.diagnostics << QStringLiteral("Entrada de inventario inválida."); continue; }
        const QJsonObject record = entry.toObject();
        const QString relative = requiredString(record, QStringLiteral("path"), out.diagnostics);
        const QString hash = requiredString(record, QStringLiteral("sha256"), out.diagnostics).toLower();
        if (!QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(hash).hasMatch())
            out.diagnostics << QStringLiteral("SHA-256 inválido en inventario: %1.").arg(relative);
        const QString key = QDir::cleanPath(relative).toLower();
        if (expected.contains(key)) out.diagnostics << QStringLiteral("Ruta repetida en inventario: %1.").arg(relative);
        expected.insert(key);
        const QString file = containedFile(out.root, relative, out.diagnostics, QStringLiteral("inventario"));
        if (file.isEmpty()) continue;
        QFile input(file);
        QCryptographicHash digest(QCryptographicHash::Sha256);
        if (!input.open(QIODevice::ReadOnly) || !digest.addData(&input) || QString::fromLatin1(digest.result().toHex()) != hash)
            out.diagnostics << QStringLiteral("Componente corrupto: %1. Extraiga otra copia del kit.").arg(relative);
    }
    QDirIterator it(out.root, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink() || info.isJunction()) out.diagnostics << QStringLiteral("Reparse point en kit: %1. Use un kit sin enlaces.").arg(path);
        if (info.isFile() && QDir::fromNativeSeparators(path) != QDir::fromNativeSeparators(manifestFile)) {
            const QString relative = QDir::fromNativeSeparators(QDir(out.root).relativeFilePath(path));
            if (!expected.contains(relative.toLower())) out.diagnostics << QStringLiteral("Archivo ajeno al inventario: %1. Use una extracción íntegra.").arg(relative);
        }
    }
    const QJsonObject components = manifest.value(QStringLiteral("components")).toObject();
    // Match Assert-KitCompatibility/Get-KitPaths in the historical artest.ps1.
    const QJsonObject pythonRuntime = components.value(QStringLiteral("pythonRuntime")).toObject();
    const QJsonObject runtime = components.value(QStringLiteral("runtime")).toObject();
    const QJsonObject authoring = components.value(QStringLiteral("authoring")).toObject();
    const QJsonObject registration = components.value(QStringLiteral("registration")).toObject();
    if (!QRegularExpression(QStringLiteral("^3\\.13\\.")).match(pythonRuntime.value(QStringLiteral("version")).toString()).hasMatch() ||
        pythonRuntime.value(QStringLiteral("architecture")).toString() != QStringLiteral("x64") ||
        !pythonRuntime.value(QStringLiteral("gilEnabled")).isBool() || !pythonRuntime.value(QStringLiteral("gilEnabled")).toBool() ||
        runtime.value(QStringLiteral("configuration")).toString() != QStringLiteral("Release") ||
        authoring.value(QStringLiteral("tool")).toString() != QStringLiteral("authoring.ps1") ||
        registration.value(QStringLiteral("tool")).toString() != QStringLiteral("registration.ps1") ||
        authoring.value(QStringLiteral("languages")).toArray() != QJsonArray{"python", "cpp"} ||
        authoring.value(QStringLiteral("variants")).toArray() != QJsonArray{"driver-command", "driver-only", "command-only"})
        out.diagnostics << QStringLiteral("Declaraciones del kit histórico incompatibles: requiere Python 3.13 x64 con GIL, runtime Release y tooling de autoría/registro. Extraiga una copia íntegra.");
    const auto declaredFile = [&](const QString &component, const QString &field) {
        const QString label = component + '.' + field;
        const QString relative = requiredString(components.value(component).toObject(), field, out.diagnostics);
        const QString resolved = containedFile(out.root, relative, out.diagnostics, label);
        if (!expected.contains(QDir::cleanPath(relative).toLower()))
            out.diagnostics << QStringLiteral("%1 no referencia un archivo del inventario. Restaure el manifiesto histórico íntegro.").arg(label);
        return resolved;
    };
    declaredFile(QStringLiteral("runtime"), QStringLiteral("cli"));
    declaredFile(QStringLiteral("runtime"), QStringLiteral("engine"));
    declaredFile(QStringLiteral("pythonSdk"), QStringLiteral("wheel"));
    declaredFile(QStringLiteral("authoring"), QStringLiteral("tool"));
    declaredFile(QStringLiteral("registration"), QStringLiteral("tool"));
    const QJsonObject native = components.value(QStringLiteral("nativeSdk")).toObject();
    if (native.value(QStringLiteral("version")).toString() != QStringLiteral("0.4.0") ||
        native.value(QStringLiteral("engineApi")).toString() != QStringLiteral("0.4") ||
        native.value(QStringLiteral("nativeExtensionAbi")).toString() != QStringLiteral("0.2") ||
        components.value(QStringLiteral("pythonSdk")).toObject().value(QStringLiteral("version")).toString() != QStringLiteral("0.2.0"))
        out.diagnostics << QStringLiteral("Versiones de componentes incompatibles. Use un kit 0.4.0 íntegro.");
    const QString nativeManifest = containedFile(out.root, native.value(QStringLiteral("root")).toString() + QStringLiteral("/sdk-manifest.json"), out.diagnostics, QStringLiteral("SDK nativo"));
    if (!nativeManifest.isEmpty()) {
        QFile input(nativeManifest);
        input.open(QIODevice::ReadOnly);
        QCryptographicHash digest(QCryptographicHash::Sha256);
        digest.addData(&input);
        const QString hash = QString::fromLatin1(digest.result().toHex());
        if (hash != native.value(QStringLiteral("manifestSha256")).toString().toLower())
            out.diagnostics << QStringLiteral("Inventario SDK nativo corrupto. Extraiga otra copia del kit.");
        const QString versionFile = containedFile(out.root, native.value(QStringLiteral("versionFile")).toString(),
                                                   out.diagnostics, QStringLiteral("versión SDK nativo"));
        const QJsonObject nativeVersion = versionFile.isEmpty() ? QJsonObject{} : readObject(versionFile, out.diagnostics);
        if (nativeVersion.value(QStringLiteral("sdkVersion")).toString() != native.value(QStringLiteral("version")).toString() ||
            nativeVersion.value(QStringLiteral("engineApi")).toString() != native.value(QStringLiteral("engineApi")).toString() ||
            nativeVersion.value(QStringLiteral("nativeExtensionAbi")).toString() != native.value(QStringLiteral("nativeExtensionAbi")).toString())
            out.diagnostics << QStringLiteral("Versión SDK nativo incompatible. Use un kit íntegro de la versión declarada.");
    }
    out.nativeSdk = QDir(out.root).filePath(native.value(QStringLiteral("root")).toString());
    out.python = declaredFile(QStringLiteral("pythonRuntime"), QStringLiteral("executable"));
    out.projectTool = declaredFile(QStringLiteral("pythonTools"), QStringLiteral("projectTool"));
    out.valid = out.diagnostics.isEmpty();
    return out;
}
}

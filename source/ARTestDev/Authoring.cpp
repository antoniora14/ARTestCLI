#include "Authoring.h"
#include "SdkLocation.h"

#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QResource>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QXmlStreamWriter>
#include <QtEndian>
#include <QScopeGuard>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static void initializeAuthoringResources() { Q_INIT_RESOURCE(authoring); }

namespace ARTestDev {
namespace {
void require(bool condition, const QString &message) {
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
}
bool ordinaryPath(QString path) {
    if (!QDir::isAbsolutePath(path) || path.startsWith(QStringLiteral("//"))) return false;
    for (;;) {
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path) return true;
        path = parent;
    }
}
bool under(const QString &path, const QString &root) {
    return path.compare(root, Qt::CaseInsensitive) == 0 || path.startsWith(root + '/', Qt::CaseInsensitive);
}
QString read(const QString &path) {
    require(ordinaryPath(path), QStringLiteral("Reparse point no permitido: %1").arg(path));
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size() <= 2 * 1024 * 1024,
            QStringLiteral("No se puede leer el recurso (máximo 2 MiB): %1").arg(path));
    return QString::fromUtf8(file.readAll());
}
void write(const QString &path, const QString &text) {
    require(ordinaryPath(path), QStringLiteral("Destino con reparse point: %1").arg(path));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("No se puede escribir: %1").arg(path));
    const QByteArray bytes = text.toUtf8();
    require(file.write(bytes) == bytes.size() && file.flush(), QStringLiteral("Escritura incompleta: %1").arg(path));
}
void json(const QString &path, const QJsonObject &value) {
    write(path, QString::fromUtf8(QJsonDocument(value).toJson()));
}
QString slug(const QString &name) {
    QString result;
    for (const QChar ch : name.normalized(QString::NormalizationForm_D)) {
        if (ch.category() == QChar::Mark_NonSpacing) continue;
        const QChar lower = ch.toLower();
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) result += lower;
        else if (!result.isEmpty() && !result.endsWith('-')) result += '-';
    }
    while (result.endsWith('-')) result.chop(1);
    if (result.isEmpty()) result = QStringLiteral("extension");
    if (result.front().isDigit()) result.prepend(QStringLiteral("extension-"));
    return result;
}
void copyTemplate(const QString &source, const QString &destination) {
    require(ordinaryPath(source) && QFileInfo(source).isDir(), QStringLiteral("Plantilla ausente o con enlaces: %1").arg(source));
    require(QDir().mkdir(destination), QStringLiteral("No se puede crear el directorio temporal: %1").arg(destination));
    QDirIterator it(source, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    int count = 0;
    qint64 size = 0;
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        require(++count <= 256 && (size += info.size()) <= 8 * 1024 * 1024 && ordinaryPath(path),
                QStringLiteral("Plantilla con enlaces o demasiado grande: %1").arg(source));
        const QString target = destination + '/' + QDir(source).relativeFilePath(path);
        require(info.isDir() ? QDir().mkdir(target) : QFile::copy(path, target), QStringLiteral("No se puede copiar: %1").arg(path));
        if (info.isFile()) QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner);
    }
}
void replace(QString &text, const QString &from, const QString &to) {
    require(text.contains(from), QStringLiteral("Plantilla incompatible: falta %1").arg(from));
    text.replace(from, to);
}
void removeRegistration(QString &text, const QString &kind) {
    const qsizetype begin = text.indexOf(QStringLiteral("    extension.Add") + kind);
    const qsizetype end = text.indexOf(QStringLiteral("    });"), begin);
    require(begin >= 0 && end > begin, QStringLiteral("Registro de plantilla incompatible: %1").arg(kind));
    text.remove(begin, end + 7 - begin);
}
QString driverId(const Creation &c) { return c.extensionId + QStringLiteral(".driver.simulated-source"); }
QString commandId(const Creation &c) { return c.extensionId + QStringLiteral(".command.measure-value"); }
QString contractId(const Creation &c) {
    return c.request.variant == QStringLiteral("command-only") ? QStringLiteral("com.example.artest.contract.value-source.v1")
        : c.extensionId + QStringLiteral(".contract.simulated-source.v1");
}
void nativeProject(Creation &c) {
    const QString root = c.staging + QStringLiteral("/project");
    copyTemplate(c.request.kit.nativeSdk + QStringLiteral("/templates/ARTestExtension"), root);
    QString stem;
    for (QString part : slug(c.request.name).split('-')) { part[0] = part[0].toUpper(); stem += part; }
    const QString oldName = QStringLiteral("ARTestExtensionStarter.vcxproj");
    c.projectFile = stem.compare(QStringLiteral("ARTestExtensionStarter"), Qt::CaseInsensitive) == 0 ? oldName : stem + QStringLiteral(".vcxproj");
    QString project = read(root + '/' + oldName);
    replace(project, QStringLiteral("ARTestExtensionStarter"), stem);
    if (c.request.kit.origin == Kit::Origin::DevelopmentStaging &&
        QFileInfo::exists(c.request.kit.root + QStringLiteral("/ARTestDevNative.exe"))) {
        write(root + QStringLiteral("/.gitignore"), read(root + QStringLiteral("/.gitignore")) + QStringLiteral("\n.artest/\n"));
        replace(project, QStringLiteral("<Import Project=\"$(ARTestSDKRoot)\\build\\native\\ARTestMetadata.targets\" Condition=\"Exists('$(ARTestSDKRoot)\\build\\native\\ARTestMetadata.targets')\" />"),
            QStringLiteral("<Import Project=\"$(ARTestSDKRoot)\\build\\native\\ARTestDevNative.targets\" Condition=\"'$(ARTestDevInner)'!='true'\" />\n"
                           "  <ItemDefinitionGroup Condition=\"'$(ARTestMetadataBuild)'=='true'\"><ClCompile><PreprocessorDefinitions>ARTEST_METADATA_GENERATOR;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile><Link><SubSystem>Console</SubSystem></Link></ItemDefinitionGroup>\n"
                           "  <PropertyGroup><DisableFastUpToDateCheck>true</DisableFastUpToDateCheck></PropertyGroup>"));
    }
    replace(project, QStringLiteral("{52B2D553-945F-4788-ACD0-6DFAB4EE0C2A}"), QUuid::createUuid().toString().toUpper());
    QString definition = read(root + QStringLiteral("/Extension.cpp"));
    replace(definition, QStringLiteral("com.example.artest.extension.starter"), c.extensionId);
    QString title = c.request.name + QStringLiteral(" extension");
    title.replace('\\', QStringLiteral("\\\\")).replace('"', QStringLiteral("\\\""));
    replace(definition, QStringLiteral("ARTest extension starter"), title);
    const bool driver = c.request.variant != QStringLiteral("command-only");
    const bool command = c.request.variant != QStringLiteral("driver-only");
    if (!driver || !command) {
        const QString header = driver ? QStringLiteral("ReadValueCommand.h") : QStringLiteral("SimulatedValueSource.h");
        replace(definition, QStringLiteral("#include \"") + header + QStringLiteral("\""), {});
        removeRegistration(definition, driver ? QStringLiteral("Command") : QStringLiteral("Driver"));
        project.remove(QRegularExpression(QStringLiteral("[ \\t]*<ClInclude Include=\"") + QRegularExpression::escape(header) + QStringLiteral("\" />\\r?\\n")));
        project.remove(QRegularExpression(QStringLiteral("[ \\t]*<None Include=\"MultipleInstruments.json\" />\\r?\\n")));
        require(QFile::remove(root + '/' + header) && QFile::remove(root + QStringLiteral("/MultipleInstruments.json")), QStringLiteral("No se pudo adaptar la variante nativa."));
    }
    if (driver) {
        replace(definition, QStringLiteral("com.example.artest.driver.sim-value-source"), driverId(c));
        replace(definition, QStringLiteral("com.example.artest.schema.sim-value-source.configuration.v1"), c.extensionId + QStringLiteral(".configuration.v1"));
        QString behavior = read(root + QStringLiteral("/SimulatedValueSource.h"));
        replace(behavior, QStringLiteral("com.example.artest.instrument.value-source.v1/read"), contractId(c) + QStringLiteral("/read"));
        write(root + QStringLiteral("/SimulatedValueSource.h"), behavior);
    }
    if (command) {
        replace(definition, QStringLiteral("com.example.artest.command.read-value"), commandId(c));
        replace(definition, QStringLiteral("com.example.artest.schema.read-value.parameters.v1"), c.extensionId + QStringLiteral(".parameters.v1"));
        QString behavior = read(root + QStringLiteral("/ReadValueCommand.h"));
        replace(behavior, QStringLiteral("com.example.artest.contract.value-source.v1"), contractId(c));
        if (driver) replace(behavior, QStringLiteral("com.example.artest.instrument.value-source.v1/read"), contractId(c) + QStringLiteral("/read"));
        write(root + QStringLiteral("/ReadValueCommand.h"), behavior);
    }
    replace(definition, QStringLiteral("com.example.artest.contract.value-source.v1"), contractId(c));
    write(root + QStringLiteral("/Extension.cpp"), definition);
    if (driver && command) {
        QString multiple = read(root + QStringLiteral("/MultipleInstruments.json"));
        replace(multiple, QStringLiteral("com.example.artest.driver.sim-value-source"), driverId(c));
        replace(multiple, QStringLiteral("com.example.artest.command.read-value"), commandId(c));
        write(root + QStringLiteral("/MultipleInstruments.json"), multiple);
    }
    write(root + '/' + c.projectFile, project);
    if (c.projectFile != oldName) require(QFile::remove(root + '/' + oldName), QStringLiteral("No se pudo renombrar el proyecto."));
    QString props;
    QXmlStreamWriter xml(&props);
    xml.writeStartElement(QStringLiteral("Project")); xml.writeStartElement(QStringLiteral("PropertyGroup"));
    xml.writeTextElement(QStringLiteral("ARTestSDKRoot"), c.request.kit.nativeSdk);
    xml.writeEndElement(); xml.writeEndElement();
    write(root + QStringLiteral("/ARTestSDK.local.props"), props);
    write(root + QStringLiteral("/.gitignore"), read(root + QStringLiteral("/.gitignore")) + QStringLiteral("\nartest-sdk-project.local.json\n"));
}
void planAndConfig(const Creation &c) {
    const bool python = c.request.language == QStringLiteral("python");
    const bool driver = c.request.variant != QStringLiteral("command-only");
    const bool command = c.request.variant != QStringLiteral("driver-only");
    const QString root = c.staging + QStringLiteral("/project");
    const QString plan = python ? QStringLiteral("plan/measurement.json") : QStringLiteral("TestPlan.json");
    const QString instrument = !driver ? QStringLiteral("CompatibleSource1") : python ? QStringLiteral("SimulatedSource1") : QStringLiteral("ValueSource1");
    const QString external = QStringLiteral("com.example.artest.driver.sim-value-source");
    if (!python || !driver || !command) {
        QJsonObject config = python && driver ? QJsonObject{{"value", 5.0}} : QJsonObject{{"initialValue", 42}};
        QJsonArray commands;
        if (command) {
            QJsonObject step{{"stepId", 1}, {"name", commandId(c)}, {"instrument", instrument}, {"params", QJsonObject{{"factor", 2}}}};
            if (python) step.insert("policy", QJsonObject{{"maxAttempts", 1}, {"timeoutMs", 2000}, {"onFailure", "stop"}});
            commands.append(step);
        }
        json(root + '/' + plan, {{"format", "ARTest.Script"}, {"version", 1},
             {"instruments", QJsonArray{QJsonObject{{"id", instrument}, {"type", driver ? driverId(c) : external}, {"config", config}}}}, {"commands", commands}});
    }
    QJsonObject guided{{"schema", "artest.schema.sdk-authoring-project.v1"}, {"schemaVersion", 1},
        {"name", c.request.name}, {"language", c.request.language}, {"variant", c.request.variant},
        {"extensionId", c.extensionId}, {"driverId", driver ? QJsonValue(driverId(c)) : QJsonValue()},
        {"commandId", command ? QJsonValue(commandId(c)) : QJsonValue()}, {"contractId", contractId(c)},
        {"externalDriverId", driver ? QJsonValue() : QJsonValue(external)}, {"plan", plan}};
    if (!python) guided.insert("projectFile", c.projectFile);
    json(root + QStringLiteral("/artest-sdk-project.json"), guided);
}
void pythonVariant(const Creation &c) {
    if (c.request.variant == QStringLiteral("driver-command")) return;
    initializeAuthoringResources();
    QFile resource(c.request.variant == QStringLiteral("driver-only") ? QStringLiteral(":/authoring/driver.py.in") : QStringLiteral(":/authoring/command.py.in"));
    require(resource.open(QIODevice::ReadOnly), QStringLiteral("Falta recurso de variante Python."));
    QString text = QString::fromUtf8(resource.readAll());
    text.replace(QStringLiteral("@EXTENSION@"), c.extensionId).replace(QStringLiteral("@DRIVER@"), driverId(c))
        .replace(QStringLiteral("@COMMAND@"), commandId(c)).replace(QStringLiteral("@CONTRACT@"), contractId(c));
    write(c.staging + QStringLiteral("/project/src/extension.py"), text);
}
void candidate(QStringList &list, const QString &path) {
    if (!path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (!list.contains(canonical, Qt::CaseInsensitive)) list << canonical;
    }
}
bool executableImage(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray dos = file.read(64);
    if (dos.size() != 64 || dos.left(2) != "MZ") return false;
    const quint32 offset = qFromLittleEndian<quint32>(dos.constData() + 60);
    if (offset > 1024 * 1024 || !file.seek(offset)) return false;
    const QByteArray pe = file.read(24);
    if (pe.size() != 24 || pe.left(4) != QByteArray("PE\0\0", 4)) return false;
    const quint16 machine = qFromLittleEndian<quint16>(pe.constData() + 4);
    const quint16 flags = qFromLittleEndian<quint16>(pe.constData() + 22);
    return (machine == IMAGE_FILE_MACHINE_AMD64 || machine == IMAGE_FILE_MACHINE_I386) &&
        (flags & IMAGE_FILE_EXECUTABLE_IMAGE) && !(flags & IMAGE_FILE_DLL);
}
}

QString defaultWorkspace() { return QStringLiteral("C:/Users/Public/ArtestDev"); }
QStringList validateForm(const CreateRequest &r) {
    QStringList errors;
    const QRegularExpression invalid(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|clock\\$|com[1-9]|lpt[1-9])(?:\\.|$)"), QRegularExpression::CaseInsensitiveOption);
    if (r.name.isEmpty() || r.name.size() > 80 || r.name != r.name.trimmed() || r.name.endsWith('.') ||
        invalid.match(r.name).hasMatch() || reserved.match(r.name).hasMatch())
        errors << QStringLiteral("Nombre: use un nombre de carpeta válido de 1–80 caracteres, sin espacios extremos ni nombres reservados.");
    if (r.language != QStringLiteral("cpp") && r.language != QStringLiteral("python")) errors << QStringLiteral("Seleccione Python o C++.");
    if (!QStringList{QStringLiteral("driver-command"), QStringLiteral("driver-only"), QStringLiteral("command-only")}.contains(r.variant)) errors << QStringLiteral("Seleccione una variante válida.");
    const QString workspace = QDir::cleanPath(QDir::fromNativeSeparators(r.workspace));
    if (!ordinaryPath(workspace)) errors << QStringLiteral("Workspace: use una ruta absoluta local sin reparse points.");
    if (under(workspace, QDir::cleanPath(r.kit.root)) || under(QDir::cleanPath(r.kit.root), workspace))
        errors << QStringLiteral("Mantenga el workspace separado de la instalación SDK (sin directorios anidados).");
    const QString destination = workspace + '/' + r.name;
    if (QFileInfo::exists(destination)) errors << QStringLiteral("El destino ya existe; elija otro nombre. No se reemplaza contenido.");
    if (destination.size() > 190) errors << QStringLiteral("Acorte workspace/nombre: el starter MSBuild requiere margen para sus rutas de salida.");
    if (!r.kit.valid) errors << QStringLiteral("Recursos SDK no disponibles. Repare o reinstale ARTestDev y vuelva a comprobar.");
    if (r.kit.valid) {
        const QString resource = r.language == QStringLiteral("cpp") ? r.kit.nativeSdk + QStringLiteral("/templates/ARTestExtension/ARTestExtensionStarter.vcxproj")
            : QFileInfo(r.kit.projectTool).absolutePath() + QStringLiteral("/../templates/minimal/artest-project.json");
        if (!QFileInfo(resource).isFile()) errors << QStringLiteral("Faltan los recursos de generación: %1. Repare o reinstale ARTestDev y vuelva a comprobar.").arg(resource);
    }
    if (r.language == QStringLiteral("cpp") && r.kit.nativeSdk.contains(QRegularExpression(QStringLiteral("[$%;]"))))
        errors << QStringLiteral("Ruta SDK no compatible con propiedades MSBuild: evite $, % y ;.");
    return errors;
}
Creation beginCreation(const CreateRequest &request) {
    Creation c; c.request = request;
    c.request.workspace = QDir::cleanPath(QDir::fromNativeSeparators(request.workspace));
    c.destination = c.request.workspace + '/' + request.name;
    c.diagnostics = validateForm(c.request);
    if (!c.diagnostics.isEmpty()) return c;
    try {
        // Recheck inventory at action time; a previous GUI readiness flag is not evidence.
        c.request.kit = request.kit.origin == Kit::Origin::DevelopmentStaging
            ? inspectStagingExecutable(request.kit.root + QStringLiteral("/ARTestDev.exe")) : inspectKit(request.kit.root);
        require(c.request.kit.valid, c.request.kit.diagnostics.join('\n'));
        require(QDir().mkpath(c.request.workspace) && ordinaryPath(c.request.workspace), QStringLiteral("No se puede crear el workspace. Seleccione una carpeta con permiso de escritura."));
        c.staging = c.request.workspace + QStringLiteral("/.artest-new-") + QUuid::createUuid().toString(QUuid::Id128);
        require(QDir().mkdir(c.staging), QStringLiteral("No se puede reservar el destino temporal; revise los permisos."));
        c.extensionId = QStringLiteral("local.") + QUuid::createUuid().toString(QUuid::Id128).left(16);
        if (request.language == QStringLiteral("cpp")) nativeProject(c);
        c.success = true;
    } catch (const std::exception &error) { c.diagnostics << QString::fromUtf8(error.what()); }
    return c;
}
QStringList pythonCreateArguments(const Creation &c) {
    return {QStringLiteral("-I"), QStringLiteral("-B"), c.request.kit.projectTool, QStringLiteral("create"), c.staging + QStringLiteral("/project"),
        QStringLiteral("--extension-id"), c.extensionId, QStringLiteral("--driver-id"), driverId(c), QStringLiteral("--command-id"), commandId(c), QStringLiteral("--author"), QStringLiteral("Example")};
}
Creation finishCreation(Creation c) {
    c.success = false;
    try {
        const QString root = c.staging + QStringLiteral("/project");
        require(ordinaryPath(root) && under(root, c.request.workspace), QStringLiteral("Destino temporal inseguro."));
        if (c.request.language == QStringLiteral("python")) pythonVariant(c);
        planAndConfig(c);
        write(root + QStringLiteral("/GENERATE-EDIT.md"), QStringLiteral(
            "# %1\n\nAbra este proyecto con Edit en ARTestDev. Edite %2.\n\n"
            "Generate sólo crea fuentes, configuración y Test plan; no prepara ni ejecuta.\n"
            "La integración y el build nativo sin PowerShell están pendientes de las siguientes entregas.\n"
            "No siga los comandos de build/registro del README histórico para este flujo DEV-01.2.\n")
            .arg(c.request.name, c.request.language == QStringLiteral("python") ? QStringLiteral("src/extension.py") :
                 c.request.variant == QStringLiteral("driver-only") ? QStringLiteral("SimulatedValueSource.h") :
                 c.request.variant == QStringLiteral("command-only") ? QStringLiteral("ReadValueCommand.h") : QStringLiteral("SimulatedValueSource.h y ReadValueCommand.h")));
        if (c.request.language == QStringLiteral("cpp") && QFileInfo::exists(c.request.kit.root + QStringLiteral("/ARTestDevNative.exe"))) {
            write(root + QStringLiteral("/GENERATE-EDIT.md"), QStringLiteral(
                "# %1\n\nAbra %2 en Visual Studio y seleccione Debug o Release | x64.\n"
                "Build produce DLL, metadatos y schemas en .artest/native/<config>/current/package.\n"
                "No requiere Python, PowerShell ni runtime ARTest. Los resultados están compilados;\n"
                "la validación contra una instalación CLI separada es posterior y no registra.\n"
                "Cambios de fuentes/SDK/configuración requieren Build explícito en Visual Studio.\n"
                "No siga los comandos de publicación del README histórico para este proyecto.\n"
                "No borre journals o resultados anteriores para resolver fallos de ownership.\n").arg(c.request.name, c.projectFile));
        }
        const Project project = inspectProject(root);
        require(project.valid, project.diagnostics.join('\n'));
        const QStringList errors = validateForm(c.request);
        require(errors.isEmpty(), errors.join('\n'));
        // MoveFileEx without REPLACE_EXISTING refuses even an empty competing destination.
        require(MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(root).utf16()),
                            reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(c.destination).utf16()), 0) != 0,
                QStringLiteral("No se pudo publicar; destino ocupado o sin permisos. Se conserva %1.").arg(root));
        QDir().rmdir(c.staging);
        c.success = true;
    } catch (const std::exception &error) {
        c.diagnostics << QString::fromUtf8(error.what()) << QStringLiteral("Se conserva el temporal para diagnóstico: %1").arg(c.staging);
    }
    return c;
}
QStringList behaviorFiles(const Project &project) {
    if (project.language == QStringLiteral("python")) {
        try {
            const auto config = QJsonDocument::fromJson(read(project.root + QStringLiteral("/artest-project.json")).toUtf8()).object();
            const QString module = config.value("entryPoint").toString().section(':', 0, 0);
            if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z_0-9]*(\\.[A-Za-z_][A-Za-z_0-9]*)*$")).match(module).hasMatch()) return {};
            QString relative = module; relative.replace('.', '/');
            const QString source = QDir::cleanPath(project.root + '/' + QDir::fromNativeSeparators(config.value("sourceDirectory").toString()));
            const QString file = source + '/' + relative + QStringLiteral(".py");
            if (!under(source, project.root) || !ordinaryPath(file)) return {};
            return {file};
        } catch (const std::exception &) { return {}; }
    }
    QStringList result;
    if (project.variant != QStringLiteral("command-only")) result << project.root + QStringLiteral("/SimulatedValueSource.h");
    if (project.variant != QStringLiteral("driver-only")) result << project.root + QStringLiteral("/ReadValueCommand.h");
    return result;
}
QString pythonProbeCode() {
    return QStringLiteral("import sys,platform,struct,json,importlib.util;print(json.dumps({'implementation':platform.python_implementation(),"
        "'version':list(sys.version_info[:2]),'platform':sys.platform,'machine':platform.machine(),'bits':struct.calcsize('P')*8,"
        "'gil':getattr(sys,'_is_gil_enabled',lambda:False)(),'venv':importlib.util.find_spec('venv') is not None,"
        "'pip':importlib.util.find_spec('ensurepip') is not None}))");
}
bool compatiblePython(const ProcessResult &result) {
    const auto o = QJsonDocument::fromJson(result.output).object();
    return result.status == ProcessResult::Status::Success && result.exitCode == 0 && o.value("implementation") == "CPython" &&
        o.value("version").toArray() == QJsonArray{3, 13} && o.value("platform") == "win32" && o.value("bits") == 64 &&
        o.value("machine").toString().compare(QStringLiteral("AMD64"), Qt::CaseInsensitive) == 0 &&
        o.value("gil").toBool() && o.value("venv").toBool() && o.value("pip").toBool();
}
QStringList nativePrerequisites(const QString &root, const QString &sdk) {
    QStringList missing;
    if (!QFileInfo::exists(root + QStringLiteral("/MSBuild/Current/Bin/MSBuild.exe"))) missing << QStringLiteral("MSBuild");
    bool compiler = false, toolset = false, windows = false;
    for (const QString &version : QDir(root + QStringLiteral("/VC/Tools/MSVC")).entryList({QStringLiteral("14.5*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        compiler |= QFileInfo::exists(root + QStringLiteral("/VC/Tools/MSVC/") + version + QStringLiteral("/bin/Hostx64/x64/cl.exe")) &&
                    QFileInfo::exists(root + QStringLiteral("/VC/Tools/MSVC/") + version + QStringLiteral("/lib/x64/libcpmt.lib"));
    for (const QString &version : QDir(root + QStringLiteral("/MSBuild/Microsoft/VC")).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        toolset |= QFileInfo::exists(root + QStringLiteral("/MSBuild/Microsoft/VC/") + version + QStringLiteral("/Platforms/x64/PlatformToolsets/v145/Toolset.props"));
    for (const QString &version : QDir(sdk + QStringLiteral("/Include")).entryList({QStringLiteral("10.*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        windows |= QFileInfo::exists(sdk + QStringLiteral("/Include/") + version + QStringLiteral("/um/Windows.h")) &&
                   QFileInfo::exists(sdk + QStringLiteral("/Include/") + version + QStringLiteral("/ucrt/stdio.h")) &&
                   QFileInfo::exists(sdk + QStringLiteral("/Lib/") + version + QStringLiteral("/um/x64/kernel32.lib")) &&
                   QFileInfo::exists(sdk + QStringLiteral("/Lib/") + version + QStringLiteral("/ucrt/x64/ucrt.lib"));
    if (!compiler) missing << QStringLiteral("compilador/bibliotecas MSVC v145 (14.5x) Hostx64/x64");
    if (!toolset) missing << QStringLiteral("PlatformToolsets/v145 x64");
    if (!windows) missing << QStringLiteral("Windows SDK 10.x: cabeceras y bibliotecas x64");
    return missing;
}
Tools discoverTools(const QString &manualVisualStudio) {
    Tools t;
    candidate(t.python, QStandardPaths::findExecutable(QStringLiteral("python.exe")));
    for (const QString &hive : {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
        QSettings registry(hive + QStringLiteral("\\Software\\Python\\PythonCore"), QSettings::NativeFormat);
        for (const QString &version : registry.childGroups()) {
            candidate(t.python, registry.value(version + QStringLiteral("/InstallPath/ExecutablePath")).toString());
            candidate(t.python, registry.value(version + QStringLiteral("/InstallPath/.")).toString() + QStringLiteral("/python.exe"));
        }
    }
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    candidate(t.python, local + QStringLiteral("/Programs/Python/Python313/python.exe"));
    candidate(t.pythonEditors, local + QStringLiteral("/Programs/Microsoft VS Code/Code.exe"));
    candidate(t.pythonEditors, QStandardPaths::findExecutable(QStringLiteral("Code.exe")));
    for (const QString &base : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)")}) {
        candidate(t.pythonEditors, base + QStringLiteral("/Microsoft VS Code/Code.exe"));
        for (const QString &version : QDir(base + QStringLiteral("/JetBrains")).entryList({QStringLiteral("PyCharm*")}, QDir::Dirs | QDir::NoDotAndDotDot))
            candidate(t.pythonEditors, base + QStringLiteral("/JetBrains/") + version + QStringLiteral("/bin/pycharm64.exe"));
    }
    QStringList roots;
    if (!manualVisualStudio.isEmpty()) roots << QDir::cleanPath(QFileInfo(manualVisualStudio).absolutePath() + QStringLiteral("/../.."));
    const QString instances = qEnvironmentVariable("ProgramData") + QStringLiteral("/Microsoft/VisualStudio/Packages/_Instances");
    for (const QString &instance : QDir(instances).entryList(QDir::Dirs | QDir::NoDotAndDotDot).mid(0, 64)) {
        QFile file(instances + '/' + instance + QStringLiteral("/state.json"));
        if (file.open(QIODevice::ReadOnly) && file.size() < 2 * 1024 * 1024) {
            const QString root = QJsonDocument::fromJson(file.readAll()).object().value("installationPath").toString();
            if (!root.isEmpty()) roots << QDir::fromNativeSeparators(root);
        }
    }
    QSettings windows(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots"), QSettings::NativeFormat);
    const QString sdk = windows.value(QStringLiteral("KitsRoot10")).toString();
    roots.removeDuplicates();
    for (const QString &root : roots) {
        candidate(t.visualStudios, root + QStringLiteral("/Common7/IDE/devenv.exe"));
        const QStringList missing = nativePrerequisites(root, sdk);
        t.diagnostics << (missing.isEmpty() ? QStringLiteral("Componentes de extensión C++ v145 encontrados: %1").arg(root)
            : QStringLiteral("%1: faltan %2. Instale manualmente Desktop development with C++, MSVC v145 x64 y Windows SDK 10.x.").arg(root, missing.join(QStringLiteral(", "))));
    }
    if (roots.isEmpty()) t.diagnostics << QStringLiteral("C++: instale manualmente Visual Studio con MSBuild, Desktop development with C++, MSVC v145 x64 y Windows SDK 10.x. Generate no requiere compilador.");
    return t;
}
QStringList editorArguments(const QString &executable, const Project &project) {
    const QString name = QFileInfo(executable).fileName().toLower();
    if (project.language == QStringLiteral("cpp")) return {project.projectFile};
    if (name == QStringLiteral("code.exe")) return {QStringLiteral("--reuse-window"), project.root, behaviorFiles(project).value(0)};
    return {project.root, behaviorFiles(project).value(0)};
}
QString launchEditor(const QString &executable, const Project &project) {
    if (!project.valid || !QFileInfo(executable).isFile() || QFileInfo(executable).suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0)
        return QStringLiteral("IDE ausente: seleccione un ejecutable .exe instalado.");
    const QString name = QFileInfo(executable).fileName().toLower();
    if (QStringList{QStringLiteral("powershell.exe"), QStringLiteral("pwsh.exe"), QStringLiteral("cmd.exe"),
                    QStringLiteral("wscript.exe"), QStringLiteral("cscript.exe"), QStringLiteral("msiexec.exe"),
                    QStringLiteral("winget.exe"), QStringLiteral("choco.exe")}.contains(name))
        return QStringLiteral("Seleccione un IDE/editor; no se ejecutan shells ni instaladores.");
    if (name == QStringLiteral("python.exe") || name == QStringLiteral("pythonw.exe") || name == QStringLiteral("py.exe") ||
        name == QStringLiteral("msbuild.exe") || name == QStringLiteral("cl.exe"))
        return QStringLiteral("Seleccione un IDE/editor; un intérprete o compilador no es un editor.");
    if (project.language == QStringLiteral("cpp") && name != QStringLiteral("devenv.exe"))
        return QStringLiteral("C++ requiere el IDE Visual Studio (devenv.exe).");
    if (!executableImage(executable)) return QStringLiteral("IDE incompatible: seleccione un ejecutable Windows x64/x86 válido.");
    const QStringList files = behaviorFiles(project);
    if (files.isEmpty()) return QStringLiteral("No se pudo resolver el archivo de comportamiento desde la configuración portable.");
    for (const QString &file : files) if (!QFileInfo(file).isFile()) return QStringLiteral("Archivo de comportamiento ausente: %1").arg(file);
    QProcess process;
    process.setProgram(executable);
    process.setArguments(editorArguments(executable, project));
    process.setWorkingDirectory(project.root);
    DWORD previousMode = 0;
    const bool modeSet = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, &previousMode) != 0;
    const auto restoreMode = qScopeGuard([modeSet, previousMode] { if (modeSet) SetThreadErrorMode(previousMode, nullptr); });
    // Detached editors belong to the user, never to the bounded tooling supervisor.
    if (!process.startDetached()) return QStringLiteral("No se pudo abrir el IDE: %1").arg(process.errorString());
    return {};
}
}

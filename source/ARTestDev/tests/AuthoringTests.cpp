#include "Authoring.h"
#include "SdkLocation.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winioctl.h>
#include <vector>
#include <cstring>

using namespace ARTestDev;
namespace {
QByteArray bytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
void put(const QString &path, const QByteArray &value = "fixture") {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(value) != value.size()) qFatal("fixture write failed");
}
QJsonObject object(const QString &path) { return QJsonDocument::fromJson(bytes(path)).object(); }
void copyStaging(const QString &source, const QString &target) {
    QDirIterator it(source, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next(), to = target + '/' + QDir(source).relativeFilePath(from);
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QFile::copy(from, to)) qFatal("staging fixture copy failed");
    }
}
bool makeJunction(const QString &link, const QString &target) {
    if (!QDir().mkdir(link)) return false;
    const QString substitute = QStringLiteral("\\??\\") + QDir::toNativeSeparators(target);
    const QString print = QDir::toNativeSeparators(target);
    const WORD substituteSize = WORD(substitute.size() * 2), printSize = WORD(print.size() * 2);
    const WORD dataSize = WORD(8 + substituteSize + 2 + printSize + 2);
    std::vector<BYTE> buffer(8 + dataSize, 0);
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    const WORD printOffset = WORD(substituteSize + 2);
    std::memcpy(buffer.data(), &tag, 4); std::memcpy(buffer.data() + 4, &dataSize, 2);
    std::memcpy(buffer.data() + 10, &substituteSize, 2);
    std::memcpy(buffer.data() + 12, &printOffset, 2); std::memcpy(buffer.data() + 14, &printSize, 2);
    std::memcpy(buffer.data() + 16, substitute.utf16(), substituteSize);
    std::memcpy(buffer.data() + 16 + printOffset, print.utf16(), printSize);
    HANDLE directory = CreateFileW(reinterpret_cast<LPCWSTR>(link.utf16()), GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0;
    const bool ok = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, buffer.data(), DWORD(buffer.size()), nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(directory); return ok;
}
}
class AuthoringTests : public QObject {
    Q_OBJECT
    Kit kit_;
    QString python_;
private slots:
    void initTestCase() {
        const QString path = qEnvironmentVariable("ARTESTDEV_TEST_STAGING");
        if (path.isEmpty()) QSKIP("Set ARTESTDEV_TEST_STAGING for development staging generation acceptance.");
        kit_ = inspectStagingExecutable(path + "/ARTestDev.exe");
        QVERIFY2(kit_.valid, qPrintable(kit_.diagnostics.join('\n')));
        python_ = qEnvironmentVariable("ARTESTDEV_TEST_PYTHON");
    }
    void variants_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("variant");
        for (const QString &language : {QStringLiteral("cpp"), QStringLiteral("python")})
            for (const QString &variant : {QStringLiteral("driver-command"), QStringLiteral("driver-only"), QStringLiteral("command-only")})
                QTest::newRow(qPrintable(language + '/' + variant)) << language << variant;
    }
    void stagingInventory_data() {
        QTest::addColumn<QString>("fault");
        for (const char *fault : {"missing", "gitignore-unlisted", "corrupt", "additional", "duplicate", "escape", "absolute", "reparse", "ancestor", "version", "configuration", "component", "descriptor", "prohibited"})
            QTest::newRow(fault) << QString::fromLatin1(fault);
    }
    void stagingInventory() {
        QFETCH(QString, fault);
        QTemporaryDir temp;
        const QString root = temp.path() + "/SDK with spaces";
        copyStaging(kit_.root, root);
        const QString descriptor = root + "/artestdev-staging.json";
        auto manifest = object(descriptor);
        auto entries = manifest.value("files").toArray();
        const QByteArray before = bytes(descriptor);
        const Kit stale = inspectStagingExecutable(root + "/ARTestDev.exe");
        QVERIFY2(stale.valid, qPrintable(stale.diagnostics.join('\n')));
        QString executable = root + "/ARTestDev.exe";
        QString link;
        if (fault == "missing") QVERIFY(QFile::remove(root + "/python/templates/minimal/.gitignore"));
        else if (fault == "corrupt") put(root + "/python/tools/project.py", "corrupted");
        else if (fault == "additional") put(root + "/unknown.txt");
        else if (fault == "descriptor") QVERIFY(QFile::remove(descriptor));
        else if (fault == "reparse" || fault == "ancestor") {
            link = fault == "reparse" ? root + "/unexpected link" : temp.path() + "/linked SDK";
            QVERIFY(makeJunction(link, fault == "reparse" ? temp.path() : root));
            if (fault == "ancestor") executable = link + "/ARTestDev.exe";
        } else {
            if (fault == "gitignore-unlisted") {
                QVERIFY(QFile::remove(root + "/python/templates/minimal/.gitignore"));
                for (qsizetype i = entries.size(); i-- > 0;)
                    if (entries.at(i).toObject().value("path") == "python/templates/minimal/.gitignore") entries.removeAt(i);
            } else if (fault == "duplicate") entries.append(entries.first());
            else if (fault == "escape" || fault == "absolute") {
                auto entry = entries.first().toObject(); entry["path"] = fault == "escape" ? "../escape" : "C:/escape"; entries.append(entry);
            } else if (fault == "version") manifest["internalVersion"] = 2;
            else if (fault == "configuration") manifest["configuration"] = "Retail";
            else if (fault == "component") manifest["components"] = QJsonObject{{"nativeSdk", "0.1.0"}};
            else if (fault == "prohibited") {
                put(root + "/python/python.exe");
                entries.append(QJsonObject{{"path", "python/python.exe"}, {"sha256", QString(64, '0')}});
            }
            manifest["files"] = entries;
            put(descriptor, QJsonDocument(manifest).toJson());
        }
        const auto unlink = qScopeGuard([&] { if (!link.isEmpty()) RemoveDirectoryW(reinterpret_cast<LPCWSTR>(link.utf16())); });
        const QByteArray damaged = bytes(descriptor);
        const Kit invalid = inspectStagingExecutable(executable);
        QVERIFY2(!invalid.valid, qPrintable(fault));
        QVERIFY(invalid.diagnostics.join('\n').contains("Repare o reinstale"));
        QCOMPARE(bytes(descriptor), damaged);
        if (fault != "ancestor") {
            const auto creation = beginCreation({"Blocked", "cpp", "driver-command", temp.path() + "/workspace", stale});
            QVERIFY(!creation.success);
            QVERIFY(!QFileInfo::exists(temp.path() + "/workspace"));
        }
        if (fault == "missing" || fault == "corrupt" || fault == "additional" || fault == "reparse" || fault == "ancestor") QCOMPARE(damaged, before);
    }
    void movedReadOnlySdkGenerateAndEdit() {
        QTemporaryDir temp;
        const QString root = temp.path() + "/read only SDK with spaces";
        copyStaging(kit_.root, root);
        const QString previousCwd = QDir::currentPath();
        const auto restoreCwd = qScopeGuard([previousCwd] { QDir::setCurrent(previousCwd); });
        QVERIFY(QDir::setCurrent(temp.path()));
        Kit moved = inspectStagingExecutable(root + "/ARTestDev.exe");
        QVERIFY2(moved.valid, qPrintable(moved.diagnostics.join('\n')));
        const QByteArray inventory = bytes(root + "/artestdev-staging.json");
        PACL oldAcl = nullptr; PSECURITY_DESCRIPTOR oldDescriptor = nullptr, readOnlyDescriptor = nullptr;
        auto *native = reinterpret_cast<LPWSTR>(const_cast<ushort *>(root.utf16()));
        QCOMPARE(GetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &oldDescriptor), DWORD(ERROR_SUCCESS));
        QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;GRGX;;;WD)", SDDL_REVISION_1, &readOnlyDescriptor, nullptr));
        PACL readOnlyAcl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        QVERIFY(GetSecurityDescriptorDacl(readOnlyDescriptor, &present, &readOnlyAcl, &defaulted));
        const auto restoreAcl = qScopeGuard([&] {
            SetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, oldAcl, nullptr);
            LocalFree(oldDescriptor); LocalFree(readOnlyDescriptor);
        });
        QCOMPARE(SetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, readOnlyAcl, nullptr), DWORD(ERROR_SUCCESS));
        QFile denied(root + "/must-not-write.txt"); QVERIFY(!denied.open(QIODevice::WriteOnly));
        QFile deniedResource(root + "/python/tools/project.py"); QVERIFY(!deniedResource.open(QIODevice::WriteOnly | QIODevice::Append));
        for (const QString &language : {QStringLiteral("cpp"), QStringLiteral("python")}) {
            Creation c = beginCreation({language + " moved", language, "driver-command", temp.path() + "/projects", moved});
            QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
            if (language == "python") {
                QVERIFY(!python_.isEmpty());
                ProcessAdapter process; ProcessResult result; bool done = false;
                connect(&process, &ProcessAdapter::completed, this, [&](const ProcessResult &r) { result = r; done = true; });
                QVERIFY(process.start(python_, pythonCreateArguments(c)));
                QTRY_VERIFY_WITH_TIMEOUT(done, 35000);
                QVERIFY2(result.status == ProcessResult::Status::Success, result.output.constData());
            }
            c = finishCreation(c); QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
            const Project p = inspectProject(c.destination); QVERIFY(p.valid);
            // The same detached editor probe verifies actual launch for both languages.
            const QString editor = temp.path() + (language == "cpp" ? "/devenv.exe" : "/editor.exe");
            QVERIFY(QFile::copy(QCoreApplication::applicationDirPath() + "/ARTestDevEditorProbe.exe", editor));
            QVERIFY2(launchEditor(editor, p).isEmpty(), "detached editor launch failed");
            QTRY_COMPARE_WITH_TIMEOUT(QDir(p.root).entryList({"editor-*.txt"}, QDir::Files).size(), 1, 5000);
            const QString marker = QDir(p.root).entryList({"editor-*.txt"}, QDir::Files).first();
            QTRY_COMPARE_WITH_TIMEOUT(bytes(p.root + '/' + marker), QByteArray("started-still-alive"), 5000);
        }
        QCOMPARE(bytes(root + "/artestdev-staging.json"), inventory);
        QVERIFY(inspectStagingExecutable(root + "/ARTestDev.exe").valid);
        qInfo().noquote() << "Read-only relocated SDK:" << root << "Original:" << kit_.root << "CWD:" << QDir::currentPath();
    }
    void variants() {
        QFETCH(QString, language); QFETCH(QString, variant);
        QTemporaryDir temp;
        const QString previousCwd = QDir::currentPath();
        const auto restoreCwd = qScopeGuard([previousCwd] { QDir::setCurrent(previousCwd); });
        QVERIFY(QDir::setCurrent(temp.path()));
        const QByteArray inventory = bytes(kit_.root + QStringLiteral("/artestdev-staging.json"));
        CreateRequest r{QStringLiteral("ARTestExtensionStarter"), language, variant, temp.path() + QStringLiteral("/custom workspace"), kit_};
        Creation c = beginCreation(r);
        QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
        if (language == QStringLiteral("python")) {
            QVERIFY2(!python_.isEmpty(), "ARTESTDEV_TEST_PYTHON must select external CPython 3.13 x64");
            ProcessAdapter process;
            ProcessResult result; int completed = 0;
            connect(&process, &ProcessAdapter::completed, this, [&](const ProcessResult &v) { result = v; ++completed; });
            QVERIFY(process.start(python_, {"-I", "-B", "-c", pythonProbeCode()}));
            QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 20000);
            QVERIFY2(compatiblePython(result), result.output.constData());
            QVERIFY(process.start(python_, pythonCreateArguments(c), 30000));
            QTRY_COMPARE_WITH_TIMEOUT(completed, 2, 35000);
            QVERIFY2(result.status == ProcessResult::Status::Success, result.output.constData());
        }
        c = finishCreation(c);
        QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
        const Project p = inspectProject(c.destination);
        QVERIFY2(p.valid, qPrintable(p.diagnostics.join('\n')));
        const auto guided = object(c.destination + QStringLiteral("/artest-sdk-project.json"));
        const auto plan = object(p.plan);
        const bool driver = variant != QStringLiteral("command-only");
        const bool command = variant != QStringLiteral("driver-only");
        QCOMPARE(guided.value("driverId").isNull(), !driver);
        QCOMPARE(guided.value("commandId").isNull(), !command);
        QCOMPARE(plan.value("commands").toArray().size(), command ? 1 : 0);
        QCOMPARE(plan.value("instruments").toArray().size(), 1);
        const auto instrument = plan.value("instruments").toArray().first().toObject();
        QCOMPARE(instrument.value("type").toString(), driver ? guided.value("driverId").toString() : QStringLiteral("com.example.artest.driver.sim-value-source"));
        if (command) QCOMPARE(plan.value("commands").toArray().first().toObject().value("name"), guided.value("commandId"));
        for (const QString &file : behaviorFiles(p)) QVERIFY(QFileInfo(file).isFile());
        if (!driver) {
            QCOMPARE(guided.value("contractId").toString(), QStringLiteral("com.example.artest.contract.value-source.v1"));
            QVERIFY(bytes(behaviorFiles(p).first()).contains("com.example.artest.instrument.value-source.v1/read"));
        }
        const QByteArray definition = bytes(c.destination + (language == QStringLiteral("cpp") ? QStringLiteral("/Extension.cpp") : QStringLiteral("/src/extension.py")));
        QVERIFY(definition.contains(c.extensionId.toUtf8()));
        QVERIFY(!definition.contains("@EXTENSION@")); QVERIFY(!definition.contains("__ARTEST_"));
        if (language == QStringLiteral("cpp")) {
            QCOMPARE(p.projectFile, c.destination + QStringLiteral("/ARTestExtensionStarter.vcxproj"));
            QCOMPARE(definition.contains("AddDriver<"), driver); QCOMPARE(definition.contains("AddCommand<"), command);
            QVERIFY(bytes(p.projectFile).contains("v145"));
            QVERIFY(!bytes(p.projectFile).contains("ARTestEngine.Core"));
        } else {
            const QByteArray ignore = bytes(c.destination + QStringLiteral("/.gitignore"));
            QVERIFY(!ignore.isEmpty());
            QCOMPARE(ignore, bytes(kit_.root + QStringLiteral("/python/templates/minimal/.gitignore")));
            QByteArray normalized = ignore;
            normalized.replace("\r\n", "\n");
            for (const QByteArray &rule : {QByteArray("artest-project.local.json"), QByteArray(".artest/"),
                                          QByteArray("__pycache__/"), QByteArray("*.pyc")})
                QVERIFY2(normalized.split('\n').contains(rule), rule.constData());
        }
        QVERIFY(!QFileInfo::exists(c.destination + QStringLiteral("/python.exe")));
        QVERIFY(!QFileInfo::exists(c.destination + QStringLiteral("/ARTestCLI.exe")));
        QVERIFY(!QFileInfo::exists(c.destination + QStringLiteral("/ARTestEngine.dll")));
        QVERIFY(!QFileInfo::exists(c.destination + QStringLiteral("/out")));
        QVERIFY(!QFileInfo::exists(c.destination + QStringLiteral("/.venv")));
        QVERIFY(!beginCreation(r).success);
        QCOMPARE(bytes(kit_.root + QStringLiteral("/artestdev-staging.json")), inventory);
        QVERIFY(QDir::setCurrent(previousCwd));
    }
    void invalidDestinationsAndCollisions() {
        QTemporaryDir temp;
        CreateRequest r{QStringLiteral("Safe"), QStringLiteral("cpp"), QStringLiteral("driver-command"), temp.path(), kit_};
        for (const QString &name : {QStringLiteral("../escape"), QStringLiteral("CON"), QStringLiteral("a."), QStringLiteral("a/b"), QStringLiteral("a:b"), QStringLiteral(" tail ")}) {
            r.name = name; QVERIFY(!validateForm(r).isEmpty());
        }
        r.name = QStringLiteral("Safe");
        r.workspace = kit_.root + QStringLiteral("/projects"); QVERIFY(!validateForm(r).isEmpty());
        r.workspace = temp.path();
        put(temp.path() + QStringLiteral("/Safe"), "foreign"); QVERIFY(!beginCreation(r).success);
        QCOMPARE(bytes(temp.path() + QStringLiteral("/Safe")), QByteArray("foreign"));
        r.name = QStringLiteral("Another");
        Creation c = beginCreation(r); QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
        QVERIFY(QDir().mkdir(c.destination));
        QVERIFY(!finishCreation(c).success);
        QVERIFY(QFileInfo::exists(c.staging + QStringLiteral("/project/Extension.cpp")));
        r.name = QStringLiteral("artestextensionstarter");
        c = finishCreation(beginCreation(r)); QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
        r.name = QStringLiteral("ARTestExtensionStarter"); QVERIFY(!beginCreation(r).success);
        r.name = QStringLiteral("ARTest Extension Starter");
        const Creation other = finishCreation(beginCreation(r)); QVERIFY(other.success);
        QVERIFY(other.extensionId != c.extensionId);
    }
    void deniedWorkspace() {
        QTemporaryDir temp;
        const QString path = temp.path() + QStringLiteral("/denied"); QVERIFY(QDir().mkdir(path));
        PACL oldAcl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
        const auto native = reinterpret_cast<LPWSTR>(const_cast<ushort *>(path.utf16()));
        QCOMPARE(GetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &descriptor), DWORD(ERROR_SUCCESS));
        ACL empty; QVERIFY(InitializeAcl(&empty, sizeof(empty), ACL_REVISION));
        QCOMPARE(SetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, &empty, nullptr), DWORD(ERROR_SUCCESS));
        const Creation c = beginCreation({QStringLiteral("NoWrite"), QStringLiteral("cpp"), QStringLiteral("driver-command"), path, kit_});
        const DWORD restored = SetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, oldAcl, nullptr);
        LocalFree(descriptor);
        QCOMPARE(restored, DWORD(ERROR_SUCCESS)); QVERIFY(!c.success);
    }
    void reparseWorkspace() {
        QTemporaryDir temp;
        const QString target = temp.path() + QStringLiteral("/real"), link = temp.path() + QStringLiteral("/link");
        QVERIFY(QDir().mkdir(target));
        QVERIFY2(makeJunction(link, target), "Cannot create the owned junction fixture");
        QVERIFY(!validateForm({QStringLiteral("Escape"), QStringLiteral("cpp"), QStringLiteral("driver-command"), link + QStringLiteral("/child"), kit_}).isEmpty());
        QVERIFY(!QFileInfo::exists(target + QStringLiteral("/child")));
        RemoveDirectoryW(reinterpret_cast<LPCWSTR>(link.utf16()));
    }
    void prerequisitesAndRecheck() {
        QTemporaryDir temp;
        const QString vs = temp.path() + QStringLiteral("/VS"), sdk = temp.path() + QStringLiteral("/SDK");
        QCOMPARE(nativePrerequisites(vs, sdk).size(), 4);
        put(vs + QStringLiteral("/Common7/IDE/devenv.exe"));
        QCOMPARE(nativePrerequisites(vs, sdk).size(), 4);
        put(vs + QStringLiteral("/MSBuild/Current/Bin/MSBuild.exe"));
        put(vs + QStringLiteral("/VC/Tools/MSVC/14.44.1/bin/Hostx64/x64/cl.exe"));
        QCOMPARE(nativePrerequisites(vs, sdk).size(), 3);
        for (const QString &file : {QStringLiteral("/VC/Tools/MSVC/14.50.1/bin/Hostx64/x64/cl.exe"), QStringLiteral("/VC/Tools/MSVC/14.50.1/lib/x64/libcpmt.lib"), QStringLiteral("/MSBuild/Microsoft/VC/v180/Platforms/x64/PlatformToolsets/v145/Toolset.props")}) put(vs + file);
        for (const QString &file : {QStringLiteral("/Include/10.0.26100.0/um/Windows.h"), QStringLiteral("/Include/10.0.26100.0/ucrt/stdio.h"), QStringLiteral("/Lib/10.0.26100.0/um/x64/kernel32.lib"), QStringLiteral("/Lib/10.0.26100.0/ucrt/x64/ucrt.lib")}) put(sdk + file);
        QVERIFY(nativePrerequisites(vs, sdk).isEmpty());
        ProcessResult result{ProcessResult::Status::Success, 0};
        QJsonObject valid{{"implementation", "CPython"}, {"version", QJsonArray{3,13}}, {"platform", "win32"}, {"machine", "AMD64"}, {"bits",64}, {"gil",true}, {"venv",true}, {"pip",true}};
        result.output = QJsonDocument(valid).toJson(); QVERIFY(compatiblePython(result));
        for (const QString &key : {QStringLiteral("implementation"), QStringLiteral("version"), QStringLiteral("platform"), QStringLiteral("machine"), QStringLiteral("bits"), QStringLiteral("gil"), QStringLiteral("venv"), QStringLiteral("pip")}) {
            auto invalid = valid; invalid.remove(key); result.output = QJsonDocument(invalid).toJson(); QVERIFY(!compatiblePython(result));
        }
        result.output = QJsonDocument(valid).toJson(); QVERIFY(compatiblePython(result));
        result.status = ProcessResult::Status::Timeout; QVERIFY(!compatiblePython(result));
    }
    void editorArgumentsAndFailure() {
        QTemporaryDir temp;
        Project p; p.valid = true; p.root = temp.path(); p.language = QStringLiteral("python"); p.variant = QStringLiteral("driver-command");
        put(p.root + QStringLiteral("/src/extension.py"));
        put(p.root + QStringLiteral("/artest-project.json"), R"({"sourceDirectory":"src","entryPoint":"extension:define_extension"})");
        QCOMPARE(editorArguments(QStringLiteral("C:/Program Files/VS Code/Code.exe"), p), QStringList({"--reuse-window", p.root, p.root + "/src/extension.py"}));
        QCOMPARE(editorArguments(QStringLiteral("C:/Manual Editor/custom.exe"), p), QStringList({p.root, p.root + "/src/extension.py"}));
        QVERIFY(!launchEditor(QStringLiteral("Z:/missing.exe"), p).isEmpty());
        put(p.root + QStringLiteral("/broken.exe")); QVERIFY(!launchEditor(p.root + QStringLiteral("/broken.exe"), p).isEmpty());
        put(p.root + QStringLiteral("/python.exe")); QVERIFY(!launchEditor(p.root + QStringLiteral("/python.exe"), p).isEmpty());
        p.language = QStringLiteral("cpp"); p.projectFile = p.root + QStringLiteral("/My Project.vcxproj");
        QCOMPARE(editorArguments(QStringLiteral("C:/VS/devenv.exe"), p), QStringList({p.projectFile}));
    }
    void detachedEditorAlreadyRunning() {
        QTemporaryDir temp;
        Project p; p.valid = true; p.root = temp.path(); p.language = QStringLiteral("python"); p.variant = QStringLiteral("driver-command");
        put(p.root + QStringLiteral("/src/extension.py"));
        put(p.root + QStringLiteral("/artest-project.json"), R"({"sourceDirectory":"src","entryPoint":"extension:define_extension"})");
        const QString editor = QCoreApplication::applicationDirPath() + QStringLiteral("/ARTestDevEditorProbe.exe");
        QVERIFY2(launchEditor(editor, p).isEmpty(), "first detached editor launch failed");
        QTRY_COMPARE_WITH_TIMEOUT(QDir(p.root).entryList({"editor-*.txt"}, QDir::Files).size(), 1, 5000);
        QVERIFY(launchEditor(editor, p).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(QDir(p.root).entryList({"editor-*.txt"}, QDir::Files).size(), 2, 5000);
        for (const QString &file : QDir(p.root).entryList({"editor-*.txt"}, QDir::Files))
            QTRY_COMPARE_WITH_TIMEOUT(bytes(p.root + '/' + file), QByteArray("started-still-alive"), 5000);
    }
};
QTEST_GUILESS_MAIN(AuthoringTests)
#include "AuthoringTests.moc"

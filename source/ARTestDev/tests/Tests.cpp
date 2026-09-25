#include "Inspection.h"
#include "ProcessAdapter.h"
#include "Readiness.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <cstdio>

using namespace ARTestDev;

static void writeFile(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) qFatal("Cannot write test fixture");
    file.write(data);
}

static void writeJson(const QString &path, const QJsonObject &value) {
    writeFile(path, QJsonDocument(value).toJson());
}

class Tests : public QObject {
    Q_OBJECT
private slots:
    void projectAndPortableLocalSeparation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.path() + QStringLiteral("/path with spaces");
        QDir().mkpath(root);
        writeFile(root + QStringLiteral("/plan/test.json"), "{}");
        writeFile(root + QStringLiteral("/src/a.cpp"), "// fixture");
        writeFile(root + QStringLiteral("/sample.vcxproj"), "<Project/>");
        writeJson(root + QStringLiteral("/artest-sdk-project.json"), {
            {"schema", "artest.schema.sdk-authoring-project.v1"}, {"schemaVersion", 1},
            {"name", "Sample"}, {"language", "cpp"}, {"variant", "driver-only"},
            {"extensionId", "com.example.sample"}, {"plan", "plan/test.json"},
            {"projectFile", "sample.vcxproj"}});
        const QString prior = QDir::currentPath();
        QVERIFY(QDir::setCurrent(temp.path()));
        const Project project = inspectProject(root);
        QVERIFY(QDir::setCurrent(prior));
        QCOMPARE(project.language, QStringLiteral("cpp"));
        QCOMPARE(project.python, QString());
        QVERIFY(project.projectFile.endsWith(QStringLiteral("sample.vcxproj")));
        QVERIFY(project.valid);
        QVERIFY(project.prerequisites.join(' ').contains(QStringLiteral("MSBuild")));

        writeJson(root + QStringLiteral("/artest-sdk-project.local.json"), {{"schemaVersion", 2}, {"msbuild", "C:/missing/msbuild.exe"}});
        QVERIFY(inspectProject(root).prerequisites.join(' ').contains(QStringLiteral("schemaVersion")));
        writeJson(root + QStringLiteral("/artest-sdk-project.local.json"), {{"schemaVersion", 1}, {"msbuild", "C:/missing/msbuild.exe"}});
        QVERIFY(inspectProject(root).prerequisites.join(' ').contains(QStringLiteral("MSBuild")));
        writeJson(root + QStringLiteral("/native-sdk/sdk-manifest.json"), {{"files", QJsonArray{}}});
        writeFile(root + QStringLiteral("/ARTestSDK.local.props"),
                  QStringLiteral("<Project><PropertyGroup><ARTestSDKRoot>%1/native-sdk</ARTestSDKRoot></PropertyGroup></Project>").arg(root).toUtf8());
        writeJson(root + QStringLiteral("/artest-sdk-project.local.json"),
                  {{"schemaVersion", 1}, {"msbuild", QCoreApplication::applicationFilePath()}});
        QVERIFY2(inspectProject(root).valid, qPrintable(inspectProject(root).diagnostics.join(' ')));
        QVERIFY(inspectProject(root).prerequisites.isEmpty());
        writeJson(root + QStringLiteral("/artest-sdk-project.json"), {{"schemaVersion", 1}, {"language", "cpp"}});
        QVERIFY(!inspectProject(root).valid);
    }

    void missingAndCorruptKit() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.path() + QStringLiteral("/kit with spaces");
        QDir().mkpath(root);
        QVERIFY(!inspectKit(root).valid);
        QVERIFY(inspectKit(root).partial);
        const QByteArray native = "{\"files\":[]}";
        writeFile(root + QStringLiteral("/native-sdk/sdk-manifest.json"), native);
        const QByteArray version = "{\"sdkVersion\":\"0.4.0\",\"engineApi\":\"0.4\",\"nativeExtensionAbi\":\"0.2\"}";
        writeFile(root + QStringLiteral("/native-sdk/sdk-version.json"), version);
        writeFile(root + QStringLiteral("/python/runtime/python.exe"), "fake");
        writeFile(root + QStringLiteral("/python/tools/project.py"), "fake");
        for (const QString &relative : {QStringLiteral("runtime/ARTestCLI.exe"), QStringLiteral("runtime/ARTestEngine.dll"),
                                       QStringLiteral("python/sdk.whl"), QStringLiteral("authoring.ps1"), QStringLiteral("registration.ps1")})
            writeFile(root + '/' + relative, "fake");
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(native, QCryptographicHash::Sha256).toHex());
        QJsonArray files;
        files.append(QJsonObject{{"path", "native-sdk/sdk-manifest.json"}, {"sha256", hash}});
        for (const QString &relative : {QStringLiteral("native-sdk/sdk-version.json"), QStringLiteral("python/runtime/python.exe"),
                                        QStringLiteral("python/tools/project.py"), QStringLiteral("runtime/ARTestCLI.exe"),
                                        QStringLiteral("runtime/ARTestEngine.dll"), QStringLiteral("python/sdk.whl"),
                                        QStringLiteral("authoring.ps1"), QStringLiteral("registration.ps1")}) {
            QFile file(root + '/' + relative);
            QVERIFY(file.open(QIODevice::ReadOnly));
            files.append(QJsonObject{{"path", relative}, {"sha256", QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex())}});
        }
        QJsonObject components;
        components.insert("nativeSdk", QJsonObject{{"root", "native-sdk"}, {"manifestSha256", hash},
                          {"versionFile", "native-sdk/sdk-version.json"}, {"version", "0.4.0"},
                          {"engineApi", "0.4"}, {"nativeExtensionAbi", "0.2"}});
        components.insert("pythonRuntime", QJsonObject{{"executable", "python/runtime/python.exe"},
                          {"version", "3.13.7"}, {"architecture", "x64"}, {"gilEnabled", true}});
        components.insert("pythonSdk", QJsonObject{{"version", "0.2.0"}, {"wheel", "python/sdk.whl"}});
        components.insert("pythonTools", QJsonObject{{"projectTool", "python/tools/project.py"}});
        components.insert("runtime", QJsonObject{{"configuration", "Release"}, {"cli", "runtime/ARTestCLI.exe"}, {"engine", "runtime/ARTestEngine.dll"}});
        components.insert("authoring", QJsonObject{{"tool", "authoring.ps1"}, {"languages", QJsonArray{"python", "cpp"}},
                          {"variants", QJsonArray{"driver-command", "driver-only", "command-only"}}});
        components.insert("registration", QJsonObject{{"tool", "registration.ps1"}});
        const QJsonObject manifest{{"schema", "artest.schema.development-kit-package.v1"},
                  {"platform", "windows-x64"}, {"kitVersion", "0.4.0"}, {"stability", "evaluation"},
                  {"components", components}, {"files", files}};
        const QString manifestPath = root + QStringLiteral("/sdk-manifest.json");
        writeJson(manifestPath, manifest);
        QVERIFY2(inspectKit(root).valid, qPrintable(inspectKit(root).diagnostics.join(' ')));
        // Only declarations change: every on-disk inventory hash remains correct.
        const auto rejectDeclaration = [&](const QString &component, const QString &field, const QJsonValue &value) {
            QJsonObject changed = manifest;
            QJsonObject changedComponents = components;
            QJsonObject declaration = components.value(component).toObject();
            if (value.isUndefined()) declaration.remove(field);
            else declaration.insert(field, value);
            changedComponents.insert(component, declaration);
            changed.insert("components", changedComponents);
            writeJson(manifestPath, changed);
            const Kit kit = inspectKit(root);
            QVERIFY2(!kit.valid && !kit.partial, qPrintable(component + '.' + field));
            QVERIFY(!kit.diagnostics.join(' ').contains(QStringLiteral("corrupto")));
        };
        for (const QString &component : {QStringLiteral("runtime"), QStringLiteral("pythonRuntime"),
                                        QStringLiteral("pythonSdk"), QStringLiteral("authoring"), QStringLiteral("registration")}) {
            QJsonObject changed = manifest;
            QJsonObject changedComponents = components;
            changedComponents.remove(component);
            changed.insert("components", changedComponents);
            writeJson(manifestPath, changed);
            QVERIFY(!inspectKit(root).valid);
            const QJsonObject declaration = components.value(component).toObject();
            for (const QString &field : declaration.keys())
                rejectDeclaration(component, field, QJsonValue(QJsonValue::Undefined));
        }
        rejectDeclaration("pythonRuntime", "version", "3.12.9");
        rejectDeclaration("pythonRuntime", "architecture", "x86");
        rejectDeclaration("pythonRuntime", "gilEnabled", false);
        rejectDeclaration("pythonRuntime", "gilEnabled", "true");
        rejectDeclaration("runtime", "configuration", "Debug");
        for (const auto &reference : {qMakePair(QStringLiteral("runtime"), QStringLiteral("cli")),
                                      qMakePair(QStringLiteral("runtime"), QStringLiteral("engine")),
                                      qMakePair(QStringLiteral("pythonSdk"), QStringLiteral("wheel"))}) {
            rejectDeclaration(reference.first, reference.second, "missing.file");
            rejectDeclaration(reference.first, reference.second, "sdk-manifest.json");
            rejectDeclaration(reference.first, reference.second, "../escape.file");
        }
        QJsonObject nonportableInventory = manifest;
        QJsonArray changedFiles = files;
        QJsonObject changedFile = changedFiles.first().toObject();
        changedFile.insert("path", "native-sdk\\sdk-manifest.json");
        changedFiles[0] = changedFile;
        nonportableInventory.insert("files", changedFiles);
        writeJson(manifestPath, nonportableInventory);
        QVERIFY(!inspectKit(root).valid);
        writeJson(manifestPath, manifest);
        writeFile(root + QStringLiteral("/native-sdk/sdk-manifest.json"), "changed");
        QVERIFY(inspectKit(root).diagnostics.join(' ').contains(QStringLiteral("corrupto")));
        writeJson(root + QStringLiteral("/sdk-manifest.json"), {{"schema", "future-authoring-only"}});
        const Kit unsupported = inspectKit(root);
        QVERIFY(unsupported.partial);
        QVERIFY(!unsupported.valid);
        QVERIFY(unsupported.diagnostics.join(' ').contains(QStringLiteral("no soportado")));
    }

    void pythonConfigurationAndNoProbeDuringNativeInspection() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.path() + QStringLiteral("/python project");
        QDir().mkpath(root);
        writeFile(root + QStringLiteral("/src/extension.py"), "raise RuntimeError('must not run')");
        writeFile(root + QStringLiteral("/requirements.lock"), "");
        writeFile(root + QStringLiteral("/plan/test.json"), "{}");
        writeJson(root + QStringLiteral("/artest-sdk-project.json"), {
            {"schema", "artest.schema.sdk-authoring-project.v1"}, {"schemaVersion", 1},
            {"name", "Python sample"}, {"language", "python"}, {"variant", "driver-only"},
            {"extensionId", "com.example.python"}, {"plan", "plan/test.json"}});
        writeJson(root + QStringLiteral("/artest-project.json"), {
            {"schemaVersion", 1}, {"sourceDirectory", "src"},
            {"entryPoint", "extension:define_extension"},
            {"dependencyLock", "requirements.lock"}, {"plan", "plan/test.json"}});
        const Project missing = inspectProject(root);
        QVERIFY(missing.valid);
        QVERIFY(missing.prerequisites.join(' ').contains(QStringLiteral("local")));
        writeJson(root + QStringLiteral("/artest-project.local.json"), {
            {"schemaVersion", 1}, {"python", "missing/python.exe"},
            {"sdkWheel", "missing/sdk.whl"}, {"cliExecutable", "missing/cli.exe"}});
        const Project absentTools = inspectProject(root);
        QVERIFY(absentTools.prerequisites.join(' ').contains(QStringLiteral("sdkWheel")));
        QVERIFY(absentTools.targetDiagnostics.join(' ').contains(QStringLiteral("ARTestCLI")));
        QVERIFY(absentTools.python.startsWith(root));
        writeJson(root + QStringLiteral("/artest-project.json"), {
            {"schemaVersion", 1}, {"sourceDirectory", "src"},
            {"entryPoint", "extension:define_extension"},
            {"dependencyLock", "../outside.lock"}, {"plan", "plan/test.json"}});
        QVERIFY(inspectProject(root).diagnostics.join(' ').contains(QStringLiteral("relativo")));
    }

    void normalizedPortablePathsAndLocalPathsFromAnotherCwd() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.path() + QStringLiteral("/project with spaces");
        QDir().mkpath(root);
        writeFile(root + QStringLiteral("/plan/measurement.json"), "{}");
        writeFile(root + QStringLiteral("/requirements.lock"), "");
        QDir().mkpath(root + QStringLiteral("/src"));
        writeFile(root + QStringLiteral("/src/extension.py"), "raise RuntimeError('must not run')");
        writeJson(root + QStringLiteral("/artest-sdk-project.json"), {
            {"schema", "artest.schema.sdk-authoring-project.v1"}, {"schemaVersion", 1},
            {"name", "Paths"}, {"language", "python"}, {"variant", "driver-only"},
            {"extensionId", "local.paths"}, {"plan", "plan/../plan/measurement.json"}});
        writeJson(root + QStringLiteral("/artest-project.json"), {
            {"schemaVersion", 1}, {"sourceDirectory", "./src"},
            {"entryPoint", "extension:define_extension"},
            {"dependencyLock", "./requirements.lock"}, {"plan", "plan/../plan/measurement.json"}});
        const QString prior = QDir::currentPath();
        QVERIFY(QDir::setCurrent(temp.path()));
        const Project valid = inspectProject(root);
        QVERIFY(QDir::setCurrent(prior));
        QVERIFY2(valid.valid, qPrintable(valid.diagnostics.join(' ')));
        QVERIFY(valid.plan.endsWith(QStringLiteral("/plan/measurement.json")));

        for (const QString &source : {QStringLiteral(".\\src"), QStringLiteral("."), QStringLiteral("src\\..")}) {
            writeJson(root + QStringLiteral("/artest-sdk-project.json"), {
                {"schema", "artest.schema.sdk-authoring-project.v1"}, {"schemaVersion", 1},
                {"name", "Paths"}, {"language", "python"}, {"variant", "driver-only"},
                {"extensionId", "local.paths"}, {"plan", "plan\\measurement.json"}});
            writeJson(root + QStringLiteral("/artest-project.json"), {
                {"schemaVersion", 1}, {"sourceDirectory", source}, {"entryPoint", "extension:define_extension"},
                {"dependencyLock", ".\\requirements.lock"}, {"plan", "plan\\..\\plan\\measurement.json"}});
            QVERIFY(QDir::setCurrent(temp.path()));
            const Project windowsPaths = inspectProject(root);
            QVERIFY(QDir::setCurrent(prior));
            QVERIFY2(windowsPaths.valid, qPrintable(windowsPaths.diagnostics.join(' ')));
            QCOMPARE(windowsPaths.plan, root + QStringLiteral("/plan/measurement.json"));
        }
        const QJsonObject portable{{"schemaVersion", 1}, {"sourceDirectory", "src"},
            {"entryPoint", "extension:define_extension"}, {"dependencyLock", "requirements.lock"},
            {"plan", "plan/measurement.json"}};
        for (const QString &field : {QStringLiteral("sourceDirectory"), QStringLiteral("dependencyLock"), QStringLiteral("plan")}) {
            for (const QString &bad : {QStringLiteral("..\\outside"), QStringLiteral("src\\..\\..\\outside"),
                                      QStringLiteral("C:\\outside"), QStringLiteral("C:outside"),
                                      QStringLiteral("\\outside"), QStringLiteral("\\\\server\\share\\outside"),
                                      QStringLiteral("/outside")}) {
                QJsonObject changed = portable;
                changed.insert(field, bad);
                writeJson(root + QStringLiteral("/artest-project.json"), changed);
                QVERIFY2(!inspectProject(root).valid, qPrintable(field + ':' + bad));
            }
        }
        writeJson(root + QStringLiteral("/artest-project.json"), portable);
        for (const QString &bad : {QStringLiteral("../escape.json"), QStringLiteral("C:/escape.json"),
                                   QStringLiteral("..\\escape.json")}) {
            writeJson(root + QStringLiteral("/artest-sdk-project.json"), {
                {"schema", "artest.schema.sdk-authoring-project.v1"}, {"schemaVersion", 1},
                {"name", "Paths"}, {"language", "python"}, {"variant", "driver-only"},
                {"extensionId", "local.paths"}, {"plan", bad}});
            QVERIFY(!inspectProject(root).valid);
        }

        writeFile(root + QStringLiteral("/sample.vcxproj"), "<Project/>");
        writeFile(temp.path() + QStringLiteral("/sdk with spaces/sdk-manifest.json"), "{}");
        writeJson(root + QStringLiteral("/artest-sdk-project.json"), {
            {"schema", "artest.schema.sdk-authoring-project.v1"}, {"schemaVersion", 1},
            {"name", "Paths"}, {"language", "cpp"}, {"variant", "driver-only"},
            {"extensionId", "local.paths"}, {"plan", "./plan/measurement.json"},
            {"projectFile", "./sample.vcxproj"}});
        const QString relativeMsbuild = QDir(root).relativeFilePath(QCoreApplication::applicationFilePath());
        writeJson(root + QStringLiteral("/artest-sdk-project.local.json"),
                  {{"schemaVersion", 1}, {"msbuild", relativeMsbuild}});
        writeFile(root + QStringLiteral("/ARTestSDK.local.props"),
                  "<Project><PropertyGroup><ARTestSDKRoot>../sdk with spaces</ARTestSDKRoot></PropertyGroup></Project>");
        QVERIFY(QDir::setCurrent(temp.path()));
        const Project cpp = inspectProject(root);
        QVERIFY(QDir::setCurrent(prior));
        QVERIFY2(cpp.valid, qPrintable(cpp.diagnostics.join(' ')));
        QVERIFY2(cpp.prerequisites.isEmpty(), qPrintable(cpp.prerequisites.join(' ')));
        QCOMPARE(cpp.msbuild, QDir::cleanPath(QCoreApplication::applicationFilePath()));
        QCOMPARE(cpp.python, QString());
    }

    void structuredPythonResultsAndStaleSelection() {
        CheckTracker tracker;
        tracker.selectionChanged();
        const quint64 old = tracker.begin();
        tracker.selectionChanged();
        ProcessResult success;
        success.status = ProcessResult::Status::Success;
        success.exitCode = 0;
        success.output = R"({"operation":"check","success":true,"diagnostics":[]})";
        QVERIFY(!tracker.accept(old, success));
        const quint64 current = tracker.begin();
        QCOMPARE(int(tracker.result().status), int(CheckStatus::Pending));
        QVERIFY(tracker.accept(current, success));
        QCOMPARE(int(tracker.result().status), int(CheckStatus::Ready));
        QVERIFY(!tracker.accept(current, success));

        ProcessResult failure;
        failure.status = ProcessResult::Status::Failed;
        failure.exitCode = 1;
        failure.output = R"({"operation":"check","success":false,"diagnostics":[{"code":"PYTHON_MISSING","field":"python","cause":"not found","correction":"Install CPython 3.13 x64"},{"code":"CLI_MISSING","field":"cliExecutable","cause":"not found","correction":"Select ARTestCLI"}]})";
        const CheckResult missing = parsePythonCheck(failure);
        QCOMPARE(int(missing.status), int(CheckStatus::Missing));
        QVERIFY(missing.diagnostics.join(' ').contains(QStringLiteral("Install CPython 3.13")));
        QVERIFY(missing.targetDiagnostics.join(' ').contains(QStringLiteral("Select ARTestCLI")));
        failure.output = R"({"operation":"check","success":true,"diagnostics":[]})";
        QCOMPARE(int(parsePythonCheck(failure).status), int(CheckStatus::Failed));

        ProcessAdapter adapter;
        int completed = 0;
        CheckTracker live;
        const quint64 token = live.begin();
        connect(&adapter, &ProcessAdapter::completed, this, [&](const ProcessResult &value) {
            QVERIFY(live.accept(token, value));
            ++completed;
        });
        QVERIFY(adapter.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-check-success")}, 2000));
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 3000);
        QCOMPARE(int(live.result().status), int(CheckStatus::Ready));

        ProcessAdapter failingAdapter;
        int failures = 0;
        const quint64 failedToken = live.begin();
        connect(&failingAdapter, &ProcessAdapter::completed, this, [&](const ProcessResult &value) {
            QVERIFY(live.accept(failedToken, value));
            ++failures;
        });
        QVERIFY(failingAdapter.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-check-fail")}, 2000));
        QTRY_COMPARE_WITH_TIMEOUT(failures, 1, 3000);
        QCOMPARE(int(live.result().status), int(CheckStatus::Missing));
    }

    void processOutcomes() {
        const QString program = QCoreApplication::applicationFilePath();
        {
            ProcessAdapter adapter;
            bool done = false;
            ProcessResult result;
            connect(&adapter, &ProcessAdapter::completed, this, [&](const ProcessResult &value) { result = value; done = true; });
            QVERIFY(adapter.start(QStringLiteral("Z:/definitely-missing-artestdev-test.exe"), {}, 2000));
            QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
            QCOMPARE(int(result.status), int(ProcessResult::Status::StartFailed));
        }
        for (const auto &item : {qMakePair(QStringLiteral("--child-error"), ProcessResult::Status::Failed),
                                 qMakePair(QStringLiteral("--child-sleep"), ProcessResult::Status::Timeout),
                                 qMakePair(QStringLiteral("--child-output"), ProcessResult::Status::OutputLimit)}) {
            ProcessAdapter adapter;
            bool done = false;
            ProcessResult result;
            connect(&adapter, &ProcessAdapter::completed, this, [&](const ProcessResult &value) { result = value; done = true; });
            bool uiTick = false;
            QTimer::singleShot(20, this, [&] { uiTick = true; });
            QVERIFY(adapter.start(program, {item.first}, item.first == QStringLiteral("--child-sleep") ? 100 : 5000,
                                  item.first == QStringLiteral("--child-output") ? 100 : 1024));
            QTRY_VERIFY_WITH_TIMEOUT(uiTick, 1000);
            QTRY_VERIFY_WITH_TIMEOUT(done, 6000);
            QCOMPARE(int(result.status), int(item.second));
            if (item.second == ProcessResult::Status::OutputLimit) QVERIFY(result.output.size() <= 100);
        }
        {
            ProcessAdapter adapter(nullptr, [](QProcess *) {}, 50);
            int completed = 0;
            int settled = 0;
            ProcessResult result;
            connect(&adapter, &ProcessAdapter::completed, this, [&](const ProcessResult &value) { result = value; ++completed; });
            connect(&adapter, &ProcessAdapter::settled, this, [&] { ++settled; });
            QVERIFY(adapter.start(program, {QStringLiteral("--child-sleep-short")}, 30));
            QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 1500);
            QCOMPARE(int(result.status), int(ProcessResult::Status::TerminationUnconfirmed));
            QVERIFY(adapter.terminationUnconfirmed());
            QVERIFY(!adapter.start(program, {QStringLiteral("--child-error")}, 1000));
            QTRY_COMPARE_WITH_TIMEOUT(settled, 1, 2500);
            QCOMPARE(completed, 1);
            QVERIFY(!adapter.busy());
        }
        {
            ProcessAdapter adapter;
            ProcessResult result;
            bool done = false;
            connect(&adapter, &ProcessAdapter::completed, this, [&](const ProcessResult &value) { result = value; done = true; });
            QVERIFY(adapter.start(program, {QStringLiteral("--child-sleep")}, 5000));
            adapter.cancel();
            QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
            QCOMPARE(int(result.status), int(ProcessResult::Status::Cancelled));
        }
    }
};

int main(int argc, char **argv) {
    if (argc > 1) {
        const QByteArray mode(argv[1]);
        if (mode == "--child-error") return 17;
        if (mode == "--child-sleep") { QThread::msleep(5000); return 0; }
        if (mode == "--child-sleep-short") { QThread::msleep(500); return 0; }
        if (mode == "--child-output") { for (int i = 0; i < 300; ++i) std::fputs("0123456789\n", stdout); std::fflush(stdout); return 0; }
        if (mode == "--child-check-success") { std::fputs("{\"operation\":\"check\",\"success\":true,\"diagnostics\":[]}", stdout); std::fflush(stdout); return 0; }
        if (mode == "--child-check-fail") { std::fputs("{\"operation\":\"check\",\"success\":false,\"diagnostics\":[{\"code\":\"PYTHON_MISSING\",\"field\":\"python\",\"cause\":\"not found\",\"correction\":\"Install CPython 3.13 x64\"}]}", stdout); std::fflush(stdout); return 1; }
    }
    QCoreApplication app(argc, argv);
    Tests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "Tests.moc"

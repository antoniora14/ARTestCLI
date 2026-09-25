#include "PreparationService.h"
#include "SdkLocation.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QEventLoop>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QScopeGuard>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

using namespace ARTestDev;
namespace {
QByteArray bytes(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
void put(const QString &path, const QByteArray &value) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(value) != value.size()) qFatal("fixture write failed");
}
void copyTree(const QString &from, const QString &to) {
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) { const auto input = it.next(); put(to + '/' + QDir(from).relativeFilePath(input), bytes(input)); }
}
QString hash(const QString &path) { return QString::fromLatin1(QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex()); }
// Only newly created test fixtures are sealed; installed/historical SDKs are never repaired.
void sealFixture(const QString &path) {
    auto doc = QJsonDocument::fromJson(bytes(path)).object();
    auto records = doc.value("files").toArray();
    for (qsizetype i = 0; i < records.size(); ++i) {
        auto entry = records[i].toObject(); entry["sha256"] = hash(QFileInfo(path).absolutePath() + '/' + entry.value("path").toString()); records[i] = entry;
    }
    doc["files"] = records; put(path, QJsonDocument(doc).toJson());
}
}
class PreparationTests : public QObject {
    Q_OBJECT
    QString sdk_, python_, root_;
    int invocation_ = 0;
    PreparationResult run(const QString &project, int timeout = 120000, std::function<void(PreparationService &)> during = {}) {
        PreparationService service(sdk_ + "/ARTestDev.exe");
        PreparationResult result;
        QEventLoop loop;
        QTimer guard; guard.setSingleShot(true);
        connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(&service, &PreparationService::completed, &loop, [&](const PreparationResult &v) { result = v; loop.quit(); });
        if (!service.start(project, python_, timeout)) qFatal("service refused fixture");
        if (during) during(service);
        guard.start(150000); loop.exec();
        put(root_ + QString("/operation-%1.txt").arg(++invocation_), result.output + '\n' + result.diagnostic.toUtf8() + "\nstatus=" + QByteArray::number(int(result.status)));
        return result;
    }
    QString project(const QString &name) {
        const QString path = root_ + '/' + name;
        copyTree(sdk_ + "/python/templates/minimal", path);
        return path;
    }
private slots:
    void initTestCase() {
        sdk_ = qEnvironmentVariable("ARTESTDEV_TEST_STAGING");
        python_ = qEnvironmentVariable("ARTESTDEV_TEST_PYTHON");
        QVERIFY2(!sdk_.isEmpty() && QFileInfo::exists(python_), "Explicit staging and external CPython are required; this gate must not skip.");
        QTemporaryDir evidence(QDir::tempPath() + "/ARTestDev preparation spaces-XXXXXX");
        QVERIFY(evidence.isValid()); evidence.setAutoRemove(false); root_ = evidence.path();
        qInfo().noquote() << "Preserved preparation evidence:" << root_ << "SDK:" << sdk_ << "Python:" << python_;
        QVERIFY(QDir::setCurrent(QDir::tempPath()));
    }
    void realPreparationReuseAndFailures() {
        const auto path = project("real project");
        const QByteArray local = R"({"schemaVersion":1,"vendorPaths":{"vendor":"C:/Vendor kept"},"vendorDlls":[],"planBindings":[]})";
        put(path + "/artest-project.local.json", local);
        const auto portable = bytes(path + "/artest-project.json");
        auto first = run(path);
        QVERIFY2(first.status == PreparationResult::Status::Prepared, qPrintable(first.diagnostic + QString::fromUtf8(first.output)));
        QVERIFY(!QFileInfo::exists(sdk_ + "/ARTestCLI.exe"));
        QVERIFY(!QFileInfo::exists(sdk_ + "/ARTestEngine.dll"));
        QVERIFY(!QFileInfo::exists(sdk_ + "/python/python.exe"));
        const auto receipt = bytes(first.receipt);
        const auto receiptTime = QFileInfo(first.receipt).lastModified();
        const auto launcher = bytes(first.revision + "/environment/artest-launch.py");
        QVERIFY(launcher.contains("site-packages"));
        QVERIFY(launcher.contains(first.revision.toUtf8()) || launcher.contains(QDir::toNativeSeparators(first.revision).replace("\\", "\\\\").toUtf8()));
        auto reused = run(path);
        QVERIFY2(reused.status == PreparationResult::Status::Reused, qPrintable(reused.diagnostic + QString::fromUtf8(reused.output)));
        QCOMPARE(reused.identity, first.identity);
        QCOMPARE(bytes(first.receipt), receipt); QCOMPARE(QFileInfo(first.receipt).lastModified(), receiptTime);
        QCOMPARE(bytes(first.revision + "/environment/artest-launch.py"), launcher);
        QVERIFY(!reused.output.contains("Installing collected packages"));
        put(path + "/plan/measurement.json", bytes(path + "/plan/measurement.json") + "\n");
        QCOMPARE(run(path).status, PreparationResult::Status::Reused);
        auto source = bytes(path + "/src/extension.py");
        put(path + "/src/extension.py", source + "\n# first\n");
        auto changed = run(path); QCOMPARE(changed.status, PreparationResult::Status::Prepared);
        const auto sameSize = bytes(path + "/src/extension.py").replace("# first", "# other");
        put(path + "/src/extension.py", sameSize);
        auto same = run(path); QCOMPARE(same.status, PreparationResult::Status::Prepared);
        QVERIFY(same.identity != changed.identity);
        put(path + "/requirements.lock", bytes(path + "/requirements.lock") + "\n# changed lock\n");
        auto locked = run(path); QCOMPARE(locked.status, PreparationResult::Status::Prepared);
        QVERIFY(locked.identity != same.identity);
        const auto ready = bytes(path + "/.artest/stage3/ready.json");
        put(path + "/requirements.lock", bytes(path + "/requirements.lock") + "missing-offline-package==1.0 --hash=sha256:" + QByteArray(64, '0') + "\n");
        QCOMPARE(run(path).status, PreparationResult::Status::Failed);
        QCOMPARE(bytes(path + "/.artest/stage3/ready.json"), ready);
        QVERIFY(!QDir(path + "/.artest/stage3/work").entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
        const auto failures = QDir(path + "/.artest/stage3/incomplete").entryList({"*.json"}, QDir::Files);
        QVERIFY(!failures.isEmpty());
        const auto failure = QJsonDocument::fromJson(bytes(path + "/.artest/stage3/incomplete/" + failures.first())).object();
        QVERIFY(QFileInfo::exists(failure.value("revision").toString() + "/environment"));
        QCOMPARE(bytes(path + "/artest-project.local.json"), local);
        QCOMPARE(bytes(path + "/artest-project.json"), portable);
    }
    void corruptEnvironment() {
        const auto path = project("corrupt environment");
        const auto first = run(path); QCOMPARE(first.status, PreparationResult::Status::Prepared);
        const auto ready = bytes(path + "/.artest/stage3/ready.json");
        put(first.revision + "/environment/artest-launch.py", "corruption");
        const auto result = run(path); QCOMPARE(result.status, PreparationResult::Status::Failed);
        QVERIFY(result.diagnostic.contains("integrity failed"));
        QCOMPARE(bytes(path + "/.artest/stage3/ready.json"), ready);
    }
    void interruptedTree_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("cancel") << true; QTest::newRow("timeout") << false;
    }
    void interruptedTree() {
        QFETCH(bool, cancel);
        const auto path = project(cancel ? "cancel project" : "timeout project");
        const auto first = run(path); QCOMPARE(first.status, PreparationResult::Status::Prepared);
        const auto ready = bytes(path + "/.artest/stage3/ready.json");
        const QByteArray injected = "import subprocess, sys, time\nfrom pathlib import Path\n"
            "subprocess.Popen([sys.executable, '-I', '-B', '-c', \"import time; from pathlib import Path; time.sleep(8); Path('descendant-write.txt').write_text('escaped')\"])\n"
            "Path('metadata-started.txt').write_text('started')\ntime.sleep(30)\n";
        put(path + "/src/extension.py", injected + bytes(path + "/src/extension.py"));
        QTimer cancelTimer;
        auto result = run(path, cancel ? 60000 : 4000, [&](PreparationService &service) {
            QVERIFY(!service.start(path, python_));
            if (cancel) {
                connect(&cancelTimer, &QTimer::timeout, &service, [&] {
                    if (QFileInfo::exists(path + "/metadata-started.txt")) {
                        cancelTimer.stop();
                        QCOMPARE(run(path).status, PreparationResult::Status::Failed);
                        QCOMPARE(bytes(path + "/.artest/stage3/ready.json"), ready);
                        service.cancel();
                    }
                });
                cancelTimer.start(25);
            }
        });
        QCOMPARE(result.status, cancel ? PreparationResult::Status::Cancelled : PreparationResult::Status::Timeout);
        QVERIFY(QFileInfo::exists(path + "/metadata-started.txt"));
        QCOMPARE(bytes(path + "/.artest/stage3/ready.json"), ready);
        QVERIFY(QFileInfo::exists(path + "/.artest/stage3/preparation.lock"));
        QTest::qWait(8500);
        QVERIFY(!QFileInfo::exists(path + "/descendant-write.txt"));
        QCOMPARE(run(path).status, PreparationResult::Status::Failed);
        QCOMPARE(bytes(path + "/.artest/stage3/ready.json"), ready);
    }
    void changedDuringPreparation() {
        const auto path = project("changed while preparing");
        const auto first = run(path); QCOMPARE(first.status, PreparationResult::Status::Prepared);
        const auto ready = bytes(path + "/.artest/stage3/ready.json");
        const auto source = bytes(path + "/src/extension.py");
        put(path + "/src/extension.py", "from pathlib import Path\nPath(__file__).write_text(Path(__file__).read_text() + '\\n# mutation\\n')\n" + source);
        QCOMPARE(run(path).status, PreparationResult::Status::Failed);
        QCOMPARE(bytes(path + "/.artest/stage3/ready.json"), ready);
    }
    void sdkAndToolIdentity() {
        const QString originalSdk = sdk_;
        const auto restore = qScopeGuard([&] { sdk_ = originalSdk; });
        const QString fixture = root_ + "/SDK identity fixture";
        copyTree(sdk_, fixture); sdk_ = fixture;
        const auto path = project("identity project");
        auto first = run(path); QCOMPARE(first.status, PreparationResult::Status::Prepared);
        const auto tool = sdk_ + "/python/tools/package.py";
        put(tool, bytes(tool) + "\n# fixture tooling identity\n");
        QCOMPARE(run(path).status, PreparationResult::Status::Failed);
        sealFixture(sdk_ + "/artestdev-staging.json");
        auto tooling = run(path); QCOMPARE(tooling.status, PreparationResult::Status::Prepared);
        QVERIFY(tooling.identity != first.identity);
        const auto wheel = sdk_ + "/python/wheels/artest_python-0.2.0-py3-none-any.whl";
        put(wheel, bytes(wheel) + "\n");
        sealFixture(sdk_ + "/python/wheels/artest-offline-wheelhouse.json");
        sealFixture(sdk_ + "/artestdev-staging.json");
        auto sdkChange = run(path); QCOMPARE(sdkChange.status, PreparationResult::Status::Prepared);
        QVERIFY(sdkChange.identity != tooling.identity);
        QCOMPARE(run(path).status, PreparationResult::Status::Reused);
        const auto ready = bytes(path + "/.artest/stage3/ready.json");
        // Missing local wheel must fail before pip and cannot fall back to the network.
        QVERIFY(QFile::remove(sdk_ + "/python/wheels/protobuf-6.33.4-cp310-abi3-win_amd64.whl"));
        QCOMPARE(run(path).status, PreparationResult::Status::Failed);
        QCOMPARE(bytes(path + "/.artest/stage3/ready.json"), ready);
    }
    void interpreterIdentity() {
        const QString originalPython = python_;
        const auto restorePython = qScopeGuard([&] { python_ = originalPython; });
        const auto path = project("interpreter identity project");
        const auto first = run(path); QCOMPARE(first.status, PreparationResult::Status::Prepared);
        const auto alternate = root_ + "/external Python fixture";
        copyTree(QFileInfo(python_).absolutePath(), alternate);
        python_ = alternate + "/python.exe";
        auto changed = run(path); QCOMPARE(changed.status, PreparationResult::Status::Prepared);
        QVERIFY(changed.identity != first.identity);
        QCOMPARE(run(path).status, PreparationResult::Status::Reused);
        const auto venvTool = alternate + "/Lib/venv/__init__.py";
        put(venvTool, bytes(venvTool) + "\n# changed interpreter tooling fixture\n");
        auto toolsChanged = run(path); QCOMPARE(toolsChanged.status, PreparationResult::Status::Prepared);
        QVERIFY(toolsChanged.identity != changed.identity);
    }
    void readOnlySdk() {
        const QString originalSdk = sdk_;
        const auto restoreSdk = qScopeGuard([&] { sdk_ = originalSdk; });
        sdk_ = root_ + "/read only SDK"; copyTree(originalSdk, sdk_);
        const auto descriptor = bytes(sdk_ + "/artestdev-staging.json");
        PACL oldAcl = nullptr, readOnlyAcl = nullptr;
        PSECURITY_DESCRIPTOR oldDescriptor = nullptr, readOnlyDescriptor = nullptr;
        auto native = QDir::toNativeSeparators(sdk_).toStdWString();
        QCOMPARE(GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &oldDescriptor), DWORD(ERROR_SUCCESS));
        QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;GRGX;;;WD)", SDDL_REVISION_1, &readOnlyDescriptor, nullptr));
        BOOL present = FALSE, defaulted = FALSE;
        QVERIFY(GetSecurityDescriptorDacl(readOnlyDescriptor, &present, &readOnlyAcl, &defaulted));
        const auto restoreAcl = qScopeGuard([&] {
            SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, oldAcl, nullptr);
            LocalFree(oldDescriptor); LocalFree(readOnlyDescriptor);
        });
        QCOMPARE(SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, readOnlyAcl, nullptr), DWORD(ERROR_SUCCESS));
        QFile forbidden(sdk_ + "/must-not-write.txt"); QVERIFY(!forbidden.open(QIODevice::WriteOnly));
        QFile existing(sdk_ + "/python/tools/project.py"); QVERIFY(!existing.open(QIODevice::WriteOnly));
        const auto path = project("read only preparation");
        const auto result = run(path); QCOMPARE(result.status, PreparationResult::Status::Prepared);
        QCOMPARE(run(path).status, PreparationResult::Status::Reused);
        QVERIFY(!result.revision.startsWith(sdk_));
        QCOMPARE(bytes(sdk_ + "/artestdev-staging.json"), descriptor);
        QVERIFY(inspectStagingExecutable(sdk_ + "/ARTestDev.exe").valid);
    }
    void unconfirmedTerminationRemainsBusy() {
        TreeProcess process;
        process.suppressTerminationForTest();
        ProcessResult result; int completed = 0, settled = 0;
        connect(&process, &TreeProcess::completed, this, [&](const ProcessResult &v) { result = v; ++completed; });
        connect(&process, &TreeProcess::settled, this, [&] { ++settled; });
        QVERIFY(process.start(python_, {"-I", "-B", "-c", "import time; print('started', flush=True); time.sleep(6)"}, root_));
        QTimer::singleShot(500, &process, &TreeProcess::cancel);
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 4000);
        QCOMPARE(result.status, ProcessResult::Status::TerminationUnconfirmed);
        QVERIFY(process.busy());
        QVERIFY(!process.start(python_, {}, root_));
        QTRY_COMPARE_WITH_TIMEOUT(settled, 1, 8000);
        QVERIFY(!process.busy());
        QCOMPARE(completed, 1);
    }
    void outputIsBounded() {
        TreeProcess process;
        ProcessResult result; int completed = 0;
        connect(&process, &TreeProcess::completed, this, [&](const ProcessResult &v) { result = v; ++completed; });
        QVERIFY(process.start(python_, {"-I", "-B", "-c", "import sys; sys.stdout.write('x'*1000000); sys.stdout.flush()"}, root_, 10000, 4096));
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 15000);
        QCOMPARE(result.status, ProcessResult::Status::OutputLimit);
        QCOMPARE(result.output.size(), 4096);
        QVERIFY(!process.busy());
    }
    void commitHandshake() {
        TreeProcess process;
        ProcessResult result; int completed = 0;
        connect(&process, &TreeProcess::completed, this, [&](const ProcessResult &v) { result = v; ++completed; });
        QVERIFY(process.start(python_, {"-I", "-B", "-c", "import sys; print('ARTESTDEV_COMMIT_READY', flush=True); assert sys.stdin.readline() == 'commit\\n'; print('committed', flush=True)"}, root_, 5000, 4096, true));
        QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 10000);
        QVERIFY2(result.status == ProcessResult::Status::Success, (result.output + result.detail.toUtf8()).constData());
        QVERIFY(result.output.contains("committed"));
    }
};
QTEST_GUILESS_MAIN(PreparationTests)
#include "PreparationTests.moc"

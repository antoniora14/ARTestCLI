#include "TestPlanService.h"
#include "TestPlanDialog.h"
#include "RegistrationStore.h"
#include "Authoring.h"
#include "SdkLocation.h"
#include "TreeProcess.h"
#include "../../../tests/TestSupport/C01/Catalog.h"
#include "../../../tests/TestSupport/Fakes/ARTestDevVerdict.h"
#include <QApplication>
#include <QTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QMenu>
#include <QContextMenuEvent>
#include <QUuid>
#include <iostream>
using namespace ARTestDev;
namespace R = ARTestDev::Registration;
class TestPlanTests final : public QObject {
    Q_OBJECT
    QString root_, sdk_, cli_, python_, config_, repo_;
    int counter_ = 0;
    QString fresh() { const auto path = root_ + "/case" + QString::number(++counter_); QDir().mkpath(path); return path; }
    ProcessResult process(const QString &exe, const QStringList &args, const QString &log) {
        TreeProcess child; QEventLoop loop; ProcessResult result; QByteArray audit; quint64 observed = 0, expected = 0;
        child.enableProcessAudit();
        connect(&child, &TreeProcess::processObserved, &loop, [&](quint64, const QString &path) { ++observed; audit += path.toUtf8() + '\n'; });
        connect(&child, &TreeProcess::processAuditFinished, &loop, [&](quint64 n) { expected = n; });
        connect(&child, &TreeProcess::completed, &loop, [&](const ProcessResult &p) { result = p; if (!child.busy()) loop.quit(); });
        connect(&child, &TreeProcess::settled, &loop, &QEventLoop::quit);
        R::require(child.start(exe, args, root_, 300000, 1024 * 1024), "Could not start test child"); loop.exec();
        QCoreApplication::sendPostedEvents(&loop, QEvent::MetaCall);
        audit += "observed=" + QByteArray::number(observed) + " expected=" + QByteArray::number(expected);
        R::write(log, result.output); R::write(log + ".processes", audit);
        R::save(log + ".status", {{"status", int(result.status)}, {"exit", result.exitCode}, {"detail", result.detail}});
        R::require(expected && expected == observed && !audit.toLower().contains("unresolved") && !audit.toLower().contains("powershell") && !audit.toLower().contains("pwsh"), "Incomplete or forbidden process audit");
        return result;
    }
    Project create(const QString &root, const QString &language, const QString &variant, bool shared = false) {
        CreateRequest request{variant, language, variant, root, inspectStagingExecutable(sdk_ + "/ARTestDev.exe")};
        auto c = beginCreation(request); R::require(c.success, c.diagnostics.join('\n'));
        if (language == "python") { auto p = process(python_, pythonCreateArguments(c), root + "/generate.txt"); R::require(p.status == ProcessResult::Status::Success, QString::fromUtf8(p.output)); }
        c = finishCreation(c); R::require(c.success, c.diagnostics.join('\n'));
        auto project = inspectProject(c.destination); R::require(project.valid, project.diagnostics.join('\n'));
        if (shared) {
            const auto path = project.root + (language == "cpp" ? "/Extension.cpp" : "/src/extension.py");
            auto code = R::read(path); code.replace((project.extensionId + ".contract.simulated-source.v1").toUtf8(), "com.example.artest.contract.value-source.v1");
            if (language == "python") code.replace("READ_OPERATION = CONTRACT + \"/read\"", "READ_OPERATION = \"com.example.artest.instrument.value-source.v1/read\""); R::write(path, code);
            if (language == "cpp") { const auto h = project.root + "/SimulatedValueSource.h"; auto bytes = R::read(h); bytes.replace((project.extensionId + ".contract.simulated-source.v1").toUtf8(), "com.example.artest.contract.value-source.v1"); bytes.replace("com.example.artest.contract.value-source.v1/read", "com.example.artest.instrument.value-source.v1/read"); R::write(h, bytes); }
        }
        if (language == "cpp") build(project);
        return project;
    }
    void build(const Project &p) {
        auto result = process(qEnvironmentVariable("ARTESTDEV_TEST_MSBUILD"), {p.projectFile, "/t:Build", "/m:1", "/nr:false", "/v:minimal", "/p:Configuration=" + config_ + ";Platform=x64"}, root_ + "/build-" + QUuid::createUuid().toString(QUuid::Id128) + ".txt");
        R::require(result.status == ProcessResult::Status::Success, result.detail + " status=" + QString::number(int(result.status)) + " exit=" + QString::number(result.exitCode) + '\n' + QString::fromUtf8(result.output));
    }
    void integrate(const Project &p, const QJsonObject &target, const QString &root) {
        IntegrationRequest r; r.project = p; r.target = target; r.registryRoot = root; r.configuration = config_; r.python = p.language == "python" ? python_ : QString(); r.stagedExecutable = sdk_ + "/ARTestDev.exe";
        IntegrationService service; QEventLoop loop; IntegrationResult result;
        connect(&service, &IntegrationService::completed, &loop, [&](const IntegrationResult &v) { result = v; loop.quit(); });
        service.start(r); loop.exec(); R::require(result.success, result.diagnostic);
    }
    QJsonObject attempt(const Project &p, const QJsonObject &target, const QString &root, const QString &plan, bool sources = false, const QString &action = "execute", int timeout = 300000) {
        const auto id = QUuid::createUuid().toString(QUuid::Id128), path = root + "/request-" + id + ".json";
        R::save(path, {{"root", root}, {"project", p.root}, {"extension", p.extensionId}, {"language", p.language}, {"target", target}, {"plan", plan}, {"sources", sources}, {"action", action}, {"timeout", timeout}, {"sdk", sdk_}, {"configuration", config_}, {"python", p.language == "python" ? python_ : QString()}});
        const auto log = root + "/attempt-" + id + ".txt";
        auto result = process(QCoreApplication::applicationFilePath(), {"--probe", path}, log);
        R::require(result.status == ProcessResult::Status::Success, QString::fromUtf8(result.output));
        if (p.language == "cpp") R::require(!R::read(log + ".processes").toLower().contains("python"), "Native operation started Python");
        return R::load(path + ".result");
    }
private slots:
    void initTestCase() {
        QTemporaryDir temp(QDir::tempPath() + "/ARTestDev016-XXXXXX"); QVERIFY(temp.isValid()); temp.setAutoRemove(false); root_ = temp.path(); qInfo().noquote() << "Evidence:" << root_;
        sdk_ = qEnvironmentVariable("ARTESTDEV_TEST_STAGING"); cli_ = qEnvironmentVariable("ARTESTDEV_TEST_CLI"); python_ = qEnvironmentVariable("ARTESTDEV_TEST_PYTHON"); repo_ = qEnvironmentVariable("ARTESTDEV_TEST_REPO"); config_ = QFileInfo(sdk_).fileName();
        QVERIFY(QFileInfo::exists(cli_)); QVERIFY(QFileInfo::exists(sdk_ + "/ARTestDev.exe"));
    }
    void realPlans_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("variant");
        for (const auto &lang : {"cpp", "python"}) for (const auto &kind : {"driver-command", "command-only"}) QTest::newRow(qPrintable(QString(lang) + '-' + kind)) << QString(lang) << QString(kind);
    }
    void realPlans() {
        try {
        QFETCH(QString, language); QFETCH(QString, variant);
        const auto root = fresh(), registry = root + "/local"; const auto target = R::manualProfile(cli_, registry);
        auto p = create(root + "/projects", language, variant);
        if (language == "python" && variant == "command-only") {
            const auto source = p.root + "/src/extension.py"; auto code = R::read(source);
            QVERIFY(code.contains("return Result(\n"));
            // A simulated measurement criterion in this owned test project,
            // using the existing SDK verdict envelope and broker-based driver.
            code.replace("return Result(\n", "return Result.verdict(\n            value > 0,\n"); R::write(source, code);
        }
        auto plan = R::load(p.plan);
        if (variant == "command-only") {
            auto driver = create(root + "/driver", language, "driver-only", true); integrate(driver, target, registry);
            auto instruments = plan.value("instruments").toArray(); auto instrument = instruments[0].toObject();
            const auto manifest = R::load(target.value("catalog").toString() + "/registered-" + R::hash(driver.extensionId.toUtf8()).left(16) + "/artest-extension.json");
            instrument["type"] = manifest.value("components").toArray()[0].toObject().value("typeId");
            if (language == "python") instrument["config"] = QJsonObject{{"value", 42.0}};
            instruments[0] = instrument; plan["instruments"] = instruments; R::save(p.plan, plan);
        }
        integrate(p, target, registry);
        const auto catalog = R::inventory(target.value("catalog").toString()), config = R::inventory(target.value("configuration").toString()); const auto profile = R::read(registry + "/installations.json");
        for (bool local : {false, true}) {
            const auto result = attempt(p, target, registry, p.plan, local);
            QVERIFY2(result.value("executed").toBool(), qPrintable(result.value("diagnostic").toString()));
            QCOMPARE(result.value("report").toObject().value("status").toString(), QString("passed"));
            QCOMPARE(R::inventory(target.value("catalog").toString()), catalog); QCOMPARE(R::inventory(target.value("configuration").toString()), config); QCOMPARE(R::read(registry + "/installations.json"), profile);
        }
        auto rejectedPlan = plan; rejectedPlan["commands"] = QJsonArray{QJsonObject{{"stepId", 1}, {"name", "invalid.command"}, {"instrument", "none"}, {"params", QJsonObject{}}}}; R::save(root + "/invalid.json", rejectedPlan);
        auto rejected = attempt(p, target, registry, root + "/invalid.json"); QVERIFY(!rejected.value("executed").toBool()); QVERIFY(!rejected.value("validated").toBool());
        const auto cancelled = attempt(p, target, registry, p.plan, false, "cancel-ready"); QVERIFY(cancelled.value("validated").toBool()); QVERIFY(!cancelled.value("executed").toBool());
        const auto snapshot = attempt(p, target, registry, p.plan, false, "change-original"); QCOMPARE(snapshot.value("report").toObject().value("status").toString(), QString("passed")); R::save(p.plan, plan);
        const auto changedCatalog = attempt(p, target, registry, p.plan, false, "change-catalog"); QVERIFY(changedCatalog.value("validated").toBool()); QVERIFY(!changedCatalog.value("executed").toBool()); QVERIFY(QFile::remove(target.value("catalog").toString() + "/added-after-validation"));
        const auto source = p.root + (language == "cpp" ? "/Extension.cpp" : "/src/extension.py"); auto bytes = R::read(source);
        if (language == "python") {
            const QByteArray before = variant == "driver-command" ? "value = measurement.data[\"value\"]" : "value = source_value * parameters.factor";
            QVERIFY(bytes.contains(before)); bytes.replace(before, before + " * 2.0");
        } else bytes += "\n// edit\n";
        R::write(source, bytes);
        auto registered = attempt(p, target, registry, p.plan); QCOMPARE(registered.value("report").toObject().value("status").toString(), QString("passed"));
        auto local = attempt(p, target, registry, p.plan, true);
        if (language == "cpp") { QVERIFY(!local.value("executed").toBool()); QVERIFY(local.value("diagnostic").toString().contains("Build")); }
        else {
            QCOMPARE(local.value("report").toObject().value("status").toString(), QString("passed")); QVERIFY(local.value("revision") != registered.value("revision"));
            auto value = [](const QJsonObject &r) { return r.value("report").toObject().value("steps").toArray()[0].toObject().value("outcome").toObject().value("data").toObject().value("value").toDouble(); };
            QCOMPARE(value(registered), variant == "driver-command" ? 5.0 : 84.0); QCOMPARE(value(local), value(registered) * 2.0);
        }
        if (language == "python") {
            auto commands = plan.value("commands").toArray(); auto command = commands[0].toObject(); auto params = command.value("params").toObject(); params[variant == "driver-command" ? "minimum" : "factor"] = variant == "driver-command" ? 100.0 : 0.0; command["params"] = params; commands[0] = command; plan["commands"] = commands; R::save(p.plan, plan);
            for (bool sources : {false, true}) {
                auto failed = attempt(p, target, registry, p.plan, sources);
                QVERIFY(failed.value("executed").toBool());
                QCOMPARE(failed.value("report").toObject().value("status").toString(), QString("failed")); QVERIFY(failed.value("exit").toInt() != 0);
                QCOMPARE(R::inventory(target.value("catalog").toString()), catalog); QCOMPARE(R::inventory(target.value("configuration").toString()), config); QCOMPARE(R::read(registry + "/installations.json"), profile);
            }
        }
        QCOMPARE(R::inventory(target.value("catalog").toString()), catalog); QCOMPARE(R::inventory(target.value("configuration").toString()), config); QCOMPARE(R::read(registry + "/installations.json"), profile);
        } catch (const std::exception &e) { QFAIL(e.what()); }
    }
    void nativeEffects_data() { QTest::addColumn<QString>("mode"); for (const auto &s : {"lost", "timeout", "cancelled", "before", "ok"}) QTest::newRow(s) << QString(s); }
    void nativeVerdict_data() { QTest::addColumn<bool>("pass"); QTest::addColumn<bool>("flood"); QTest::newRow("PASS") << true << false; QTest::newRow("FAIL") << false << false; QTest::newRow("output-bound") << true << true; }
    void nativeVerdict() {
        QFETCH(bool, pass);
        QFETCH(bool, flood);
        const auto root = fresh(), registry = root + "/local", package = root + "/fixture"; const auto target = R::manualProfile(cli_, registry);
        const auto metadata = artest::sdk::GenerateMetadata(artest::tests::devverdict::Define(), "ARTestDevVerdict.dll");
        R::write(package + "/artest-extension.json", QByteArray::fromStdString(metadata.manifest.dump()));
        for (const auto &[path, schema] : metadata.schemas) R::write(package + '/' + QString::fromStdString(path), QByteArray::fromStdString(schema.dump()));
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath() + "/ARTestDevVerdict.dll", package + "/ARTestDevVerdict.dll"));
        { R::Transaction tx(target, registry); tx.stage(package, {}, "fixture-v1", "com.artest.test.devverdict", "cpp"); tx.promote(); tx.commit(); }
        Project p; p.root = root; p.extensionId = "com.artest.test.devverdict"; p.language = "cpp"; p.valid = true;
        R::save(root + "/plan.json", {{"format", "ARTest.Script"}, {"version", 1}, {"instruments", QJsonArray{}}, {"commands", QJsonArray{QJsonObject{{"stepId", 1}, {"name", "com.artest.test.command.devverdict"}, {"instrument", "NoInstrument"}, {"params", QJsonObject{{"pass", pass}, {"flood", flood}}}}}}});
        const auto result = attempt(p, target, registry, root + "/plan.json"); QVERIFY2(result.value("executed").toBool(), qPrintable(result.value("diagnostic").toString()));
        if (flood) { QCOMPARE(result.value("processStatus").toInt(), int(ProcessResult::Status::OutputLimit)); QVERIFY(QFileInfo(result.value("evidence").toString() + "/execution.txt").size() <= 1024 * 1024); return; }
        QCOMPARE(result.value("report").toObject().value("status").toString(), pass ? QString("passed") : QString("failed")); QCOMPARE(result.value("exit").toInt() == 0, pass);
    }
    void nativeEffects() {
        QFETCH(QString, mode);
        const auto root = fresh(), registry = root + "/local";
        const auto target = R::manualProfile(cli_, registry);
        artest::tests::effects::AddPackage(std::filesystem::path((root + "/generated").toStdWString()), std::filesystem::path(QFileInfo(cli_).absolutePath().toStdWString()));
        { R::Transaction tx(target, registry); tx.stage(root + "/generated/C01FaultExtension", {}, "fixture-v1", "com.artest.test.effects", "cpp"); tx.promote(); tx.commit(); }
        Project p; p.root = root; p.extensionId = "com.artest.test.effects"; p.language = "cpp"; p.valid = true;
        const auto effect = root + "/effect";
        const auto json = artest::tests::effects::Plan(std::filesystem::path(effect.toStdWString()), mode.toStdString()); R::write(root + "/plan.json", QByteArray::fromStdString(json.dump()));
        const auto result = attempt(p, target, registry, root + "/plan.json"); QVERIFY2(result.value("executed").toBool(), qPrintable(result.value("diagnostic").toString()));
        if (mode == "lost" || mode == "timeout" || mode == "cancelled") { QCOMPARE(R::read(effect), QByteArray("effect\r\n")); QCOMPARE(R::read(effect + ".calls"), QByteArray("call\r\n")); QVERIFY(QJsonDocument(result.value("report").toObject()).toJson().contains("\"indeterminate\": true")); }
        else if (mode == "before") { QVERIFY(!QFileInfo::exists(effect)); QVERIFY(result.value("exit").toInt() != 0); }
        else QCOMPARE(result.value("report").toObject().value("status").toString(), QString("passed"));
    }
    void supervision_data() { QTest::addColumn<QString>("action"); QTest::newRow("cancel") << QString("cancel-running"); QTest::newRow("timeout") << QString("execute"); QTest::newRow("unconfirmed") << QString("unconfirmed"); }
    void pythonEffects_data() { nativeEffects_data(); }
    void pythonEffects() {
        try {
            QFETCH(QString, mode);
            const auto root = fresh(), registry = root + "/local", prepared = qEnvironmentVariable("ARTEST_PYTHON_ROOT", repo_ + "/artifacts/python-c01-final");
            const auto target = R::manualProfile(cli_, registry);
            { R::Transaction tx(target, registry); tx.stage(prepared + "/test-extensions/ARTestPyFaults", prepared + "/environments/faults/artest-environment.json", "retained-c01-fixture", "com.artest.python.test-faults", "python"); tx.promote(); tx.commit(); }
            Project p; p.root = root; p.extensionId = "com.artest.python.test-faults"; p.language = "python"; p.valid = true;
            const auto effect = root + "/effect";
            auto json = artest::tests::effects::Plan(std::filesystem::path(effect.toStdWString()), mode.toStdString());
            auto bytes = QByteArray::fromStdString(json.dump()); bytes.replace("com.artest.test.driver.effects", "com.artest.python.driver.test-faults"); bytes.replace("com.artest.test.command.effects", "com.artest.python.command.test-effects"); R::write(root + "/plan.json", bytes);
            const auto result = attempt(p, target, registry, root + "/plan.json"); QVERIFY2(result.value("executed").toBool(), qPrintable(result.value("diagnostic").toString()));
            if (mode == "lost" || mode == "timeout" || mode == "cancelled") { QCOMPARE(R::read(effect), QByteArray("effect\r\n")); QCOMPARE(R::read(effect + ".calls"), QByteArray("call\r\n")); QVERIFY(QJsonDocument(result.value("report").toObject()).toJson().contains("\"indeterminate\": true")); }
            else if (mode == "before") { QVERIFY(!QFileInfo::exists(effect)); QVERIFY(result.value("exit").toInt() != 0); }
            else QCOMPARE(result.value("report").toObject().value("status").toString(), QString("passed"));
        } catch (const std::exception &e) { QFAIL(e.what()); }
    }
    void uiControls_data() { QTest::addColumn<bool>("cancel"); QTest::newRow("complete") << false; QTest::newRow("cancel") << true; }
    void uiControls() {
        QFETCH(bool, cancel);
        const auto root = fresh(), registry = root + "/local"; const auto target = R::manualProfile(cli_, registry);
        artest::tests::effects::AddPackage(std::filesystem::path((root + "/generated").toStdWString()), std::filesystem::path(QFileInfo(cli_).absolutePath().toStdWString()));
        { R::Transaction tx(target, registry); tx.stage(root + "/generated/C01FaultExtension", {}, "fixture-v1", "com.artest.test.effects", "cpp"); tx.promote(); tx.commit(); }
        Project p; p.root = root; p.extensionId = "com.artest.test.effects"; p.language = "cpp"; p.valid = true; p.plan = root + "/plan.json";
        R::save(p.plan, {{"format", "ARTest.Script"}, {"version", 1}, {"instruments", QJsonArray{}}, {"commands", QJsonArray{QJsonObject{{"stepId", 1}, {"name", "Time.WaitMs"}, {"instrument", "NoInstrument"}, {"params", QJsonObject{{"milliseconds", cancel ? 30000 : 0}}}}}}});
        const auto previous = qgetenv("ARTEST_SDK_CONFIG_ROOT"); qputenv("ARTEST_SDK_CONFIG_ROOT", registry.toUtf8());
        TestPlanDialog dialog(p, {}); qputenv("ARTEST_SDK_CONFIG_ROOT", previous); dialog.show();
        auto *mode = dialog.findChild<QComboBox *>("testPlanMode"); auto *targets = dialog.findChild<QComboBox *>("testPlanTarget"); auto *validate = dialog.findChild<QPushButton *>("validateTestPlan"); auto *execute = dialog.findChild<QPushButton *>("executeTestPlan"); auto *stop = dialog.findChild<QPushButton *>("cancelTestPlan"); auto *log = dialog.findChild<QPlainTextEdit *>("testPlanDiagnostics"); auto *status = dialog.findChild<QLabel *>("testPlanStatus");
        QTRY_VERIFY_WITH_TIMEOUT(validate->isEnabled(), 10000); QCOMPARE(mode->currentIndex(), 0); QVERIFY(!execute->isEnabled()); QVERIFY(!QFileInfo::exists(root + "/.artest/test-runs"));
        validate->click(); QVERIFY(!mode->isEnabled()); QVERIFY(!targets->isEnabled()); QVERIFY(!execute->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(execute->isEnabled(), 20000); QVERIFY(!validate->isEnabled()); QVERIFY(status->text().contains("fixture-v1"));
        const auto statusBefore = status->text();
        auto clear = [&] {
            bool found = false;
            QTimer::singleShot(0, &dialog, [&] { if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) { for (auto *action : menu->actions()) if (action->text() == "Clear") { found = true; QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center()); return; } menu->close(); } });
            QTimer escape; escape.setSingleShot(true); connect(&escape, &QTimer::timeout, &dialog, [] { if (auto *popup = QApplication::activePopupWidget()) popup->close(); }); escape.start(2000);
            const QPoint point(10, 10); QContextMenuEvent context(QContextMenuEvent::Mouse, point, log->viewport()->mapToGlobal(point)); QApplication::sendEvent(log->viewport(), &context); escape.stop(); return found && log->toPlainText().isEmpty();
        };
        QVERIFY(clear()); QCOMPARE(status->text(), statusBefore); QVERIFY(execute->isEnabled()); QVERIFY(!validate->isEnabled());
        execute->click(); QVERIFY(!execute->isEnabled());
        if (cancel) {
            QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains("Ejecutando"), 10000); QVERIFY(clear()); QVERIFY(!validate->isEnabled()); stop->click(); QVERIFY(dialog.isVisible());
        }
        QTRY_VERIFY_WITH_TIMEOUT(validate->isEnabled(), 20000); QVERIFY(!execute->isEnabled()); QVERIFY(!log->toPlainText().isEmpty());
        if (cancel) QVERIFY(status->text().contains("NO confirmados")); else QVERIFY(status->text().contains("passed"));
        const auto resultStatus = status->text(); QVERIFY(clear()); QCOMPARE(status->text(), resultStatus); dialog.close();
    }
    void supervision() {
        QFETCH(QString, action);
        const auto root = fresh(), registry = root + "/local"; const auto target = R::manualProfile(cli_, registry);
        artest::tests::effects::AddPackage(std::filesystem::path((root + "/generated").toStdWString()), std::filesystem::path(QFileInfo(cli_).absolutePath().toStdWString()));
        { R::Transaction tx(target, registry); tx.stage(root + "/generated/C01FaultExtension", {}, "fixture-v1", "com.artest.test.effects", "cpp"); tx.promote(); tx.commit(); }
        Project p; p.root = root; p.extensionId = "com.artest.test.effects"; p.language = "cpp"; p.valid = true;
        R::save(root + "/plan.json", {{"format", "ARTest.Script"}, {"version", 1}, {"instruments", QJsonArray{}}, {"commands", QJsonArray{QJsonObject{{"stepId", 1}, {"name", "Time.WaitMs"}, {"instrument", "NoInstrument"}, {"params", QJsonObject{{"milliseconds", action == "unconfirmed" ? 5000 : 30000}}}}}}});
        auto r = attempt(p, target, registry, root + "/plan.json", false, action, 1000);
        if (action == "unconfirmed") { QVERIFY(r.value("unconfirmedObserved").toBool()); QCOMPARE(r.value("processStatus").toInt(), int(ProcessResult::Status::TerminationUnconfirmed)); return; }
        QVERIFY(r.value("executed").toBool()); QCOMPARE(r.value("processStatus").toInt(), int(action == "execute" ? ProcessResult::Status::Timeout : ProcessResult::Status::Cancelled)); QVERIFY(r.value("diagnostic").toString().contains("NO confirmados"));
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv); const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--probe") {
        const auto input = R::load(args[2]); TestPlanRequest r; r.project = inspectProject(input.value("project").toString());
        if (!r.project.valid) { r.project.root = input.value("project").toString(); r.project.extensionId = input.value("extension").toString(); r.project.language = input.value("language").toString(); r.project.valid = true; }
        r.target = input.value("target").toObject(); r.registryRoot = input.value("root").toString(); r.plan = input.value("plan").toString(); r.sources = input.value("sources").toBool(); r.configuration = input.value("configuration").toString(); r.stagedExecutable = input.value("sdk").toString() + "/ARTestDev.exe"; r.python = input.value("python").toString(); r.executionTimeoutMs = input.value("timeout").toInt();
        TestPlanService service(nullptr, [input] { auto process = std::make_unique<TreeProcess>(); if (input.value("action") == "unconfirmed") process->suppressTerminationForTest(); return process; });
        bool unconfirmedObserved = false;
        QObject::connect(&service, &TestPlanService::progress, &app, [&](const QString &s) {
            std::cout << s.toUtf8().constData() << std::endl;
            if (input.value("action") == "cancel-running" && s.startsWith("Ejecutando")) QTimer::singleShot(250, &service, &TestPlanService::cancel);
            if (s.startsWith("TERMINACION NO CONFIRMADA")) {
                unconfirmedObserved = true; if (!service.busy() || service.ready() || service.start(r)) qFatal("unconfirmed operation released");
                bool blocked = false; try { R::Lock lock(r.registryRoot + "/.artest-installations.lock"); } catch (const std::exception &) { blocked = true; } if (!blocked) qFatal("unconfirmed writer lock released");
            }
        });
        QObject::connect(&service, &TestPlanService::validated, &app, [&](const TestPlanResult &validated) {
            if (!service.busy() || service.start(r)) qFatal("overlap accepted");
            if (input.value("action") == "change-original") {
                R::write(r.plan, "invalid after validation");
                QFile snapshot(validated.evidence + "/test-plan.json"); if (snapshot.open(QIODevice::WriteOnly)) qFatal("validated snapshot was writable");
            }
            if (input.value("action") == "change-catalog") R::write(r.target.value("catalog").toString() + "/added-after-validation", "mutation");
            if (input.value("action") == "cancel-ready") service.cancel(); else service.execute();
        });
        QObject::connect(&service, &TestPlanService::completed, &app, [&](const TestPlanResult &v) {
            R::save(args[2] + ".result", {{"validated", v.validated}, {"executed", v.executed}, {"revision", v.revision}, {"diagnostic", v.diagnostic}, {"evidence", v.evidence}, {"report", v.report}, {"exit", v.process.exitCode}, {"processStatus", int(v.process.status)}, {"unconfirmedObserved", unconfirmedObserved}}); app.quit();
        }); service.start(r); return app.exec();
    }
    TestPlanTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "TestPlanTests.moc"

#include "RegistrationStore.h"
#include "IntegrationService.h"
#include "Authoring.h"
#include "SdkLocation.h"
#include "TreeProcess.h"
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QUuid>
#include <QCoreApplication>
#include <iostream>
using namespace ARTestDev;
namespace R = ARTestDev::Registration;
class IntegrationTests final : public QObject {
    Q_OBJECT
    QString root_, sdk_, cli_, python_, config_;
    int count_ = 0;
    QString lastRequest_; QStringList cancellationRequests_;
    QString fresh() { const auto p = root_ + "/case-" + QString::number(++count_); QDir().mkpath(p); return p; }
    QJsonObject profile(const QString &root) {
        R::write(root + "/runtime/ARTestCLI.exe", "fixture"); R::write(root + "/runtime/ARTestEngine.dll", "fixture");
        return {{"name", "fixture"}, {"cli", root + "/runtime/ARTestCLI.exe"}, {"engine", root + "/runtime/ARTestEngine.dll"}, {"catalog", root + "/catalog"}, {"configuration", root + "/config"}};
    }
    QString package(const QString &root, const QString &id = "example") {
        const auto path = root + "/package"; R::save(path + "/artest-extension.json", {{"extensionId", id}, {"version", "1.0.0"}}); R::write(path + "/payload", "old"); return path;
    }
    void initial(const QJsonObject &p, const QString &root, const QString &pkg) {
        R::Transaction tx(p, root + "/local"); tx.recover(); tx.stage(pkg, {}, "rev1", "example", "cpp"); tx.promote(); tx.commit();
    }
    ProcessResult process(const QString &exe, const QStringList &args, const QString &log) {
        TreeProcess process; QEventLoop loop; ProcessResult result; QByteArray audit; quint64 expected = 0, observed = 0;
        process.enableProcessAudit();
        connect(&process, &TreeProcess::processAuditFinished, &loop, [&](quint64 count) { expected = count; });
        connect(&process, &TreeProcess::processObserved, &loop, [&](quint64, const QString &image) { ++observed; audit += image.toUtf8() + '\n'; });
        connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &r) { result = r; if (!process.busy()) loop.quit(); });
        connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
        if (!process.start(exe, args, root_, 300000, 1024 * 1024)) qFatal("process start"); loop.exec();
        QCoreApplication::sendPostedEvents(&loop, QEvent::MetaCall);
        audit += "observed=" + QByteArray::number(observed) + " expected=" + QByteArray::number(expected) + '\n';
        R::write(log, result.output); R::write(log + ".processes", audit);
        if (!expected || observed != expected || audit.toLower().contains("unresolved") || audit.toLower().contains("powershell") || audit.toLower().contains("pwsh")) { result.status = ProcessResult::Status::Failed; result.detail = "Incomplete process audit or forbidden descendant"; }
        return result;
    }
    IntegrationResult integrate(const Project &project, const QJsonObject &target, const QString &local, bool cancel = false) {
        const auto id = QUuid::createUuid().toString(QUuid::Id128), requestPath = local + "/request-" + id + ".json";
        lastRequest_ = requestPath;
        R::save(requestPath, {{"project", project.root}, {"target", target}, {"registryRoot", local}, {"python", project.language == "cpp" ? QString() : python_}, {"configuration", config_}, {"stagedExecutable", sdk_ + "/ARTestDev.exe"}, {"cancel", cancel}});
        const auto p = process(QCoreApplication::applicationFilePath(), {"--integration-probe", requestPath}, local + "/attempt-" + id + ".txt");
        const auto marker = p.output.lastIndexOf("ARTESTDEV_INTEGRATION="); IntegrationResult result;
        if (marker < 0) { result.diagnostic = p.detail + '\n' + QString::fromUtf8(p.output); return result; }
        const auto report = R::object(p.output.mid(marker + QByteArray("ARTESTDEV_INTEGRATION=").size()).trimmed()); result.success = report.value("success").toBool() && p.status == ProcessResult::Status::Success;
        result.target = report.value("target").toString(); result.revision = report.value("revision").toString(); result.diagnostic = report.value("diagnostic").toString();
        if (project.language == "cpp" && R::read(local + "/attempt-" + id + ".txt.processes").toLower().contains("python")) { result.success = false; result.diagnostic += "Native path launched Python"; }
        return result;
    }
private slots:
    void initTestCase() {
        QTemporaryDir temp(QDir::tempPath() + "/ARTestDev015-XXXXXX"); QVERIFY(temp.isValid()); temp.setAutoRemove(false); root_ = temp.path();
        sdk_ = qEnvironmentVariable("ARTESTDEV_TEST_STAGING"); cli_ = qEnvironmentVariable("ARTESTDEV_TEST_CLI"); python_ = qEnvironmentVariable("ARTESTDEV_TEST_PYTHON");
        config_ = QFileInfo(sdk_).fileName(); qInfo().noquote() << "Evidence:" << root_;
    }
    void strictDataAndLocks() {
        QVERIFY_THROWS_EXCEPTION(std::exception, R::object("{\"a\":1,\"a\":2}"));
        const auto root = fresh(); const auto p = profile(root); R::Transaction one(p, root + "/local");
        QVERIFY_THROWS_EXCEPTION(std::exception, R::Transaction(p, root + "/local"));
        auto missing = p; missing["engine"] = root + "/missing.dll"; QVERIFY_THROWS_EXCEPTION(std::exception, R::checkProfile(missing));
        auto nested = p; nested["configuration"] = root + "/catalog/config"; QVERIFY_THROWS_EXCEPTION(std::exception, R::checkProfile(nested));
    }
    void selectionAndPreservation() {
        const auto root = fresh(); const auto p = profile(root); const auto pkg = package(root);
        R::write(root + "/catalog/foreign/notes", "foreign"); R::save(root + "/config/python-environments.json", {{"foreign.id", "foreign-receipt"}});
        initial(p, root, pkg);
        const auto before = R::inventory(root + "/catalog"); const auto state = R::read(root + "/config/registrations.json");
        { R::Transaction tx(p, root + "/local"); tx.stage(pkg, {}, "rev1", "example", "cpp"); tx.promote(); tx.commit(); }
        QCOMPARE(R::inventory(root + "/catalog"), before); QCOMPARE(R::read(root + "/config/registrations.json"), state);
        QCOMPARE(R::load(root + "/config/python-environments.json").value("foreign.id").toString(), QString("foreign-receipt"));
        QCOMPARE(R::manualProfile(root + "/runtime", root + "/local"), p);
        QCOMPARE(R::registry(root + "/local").value("selected").toString(), QString("fixture"));
        R::write(pkg + "/payload", "new");
        { R::Transaction tx(p, root + "/local"); tx.stage(pkg, {}, "rev2", "example", "cpp"); tx.promote(); tx.commit(); }
        QCOMPARE(R::read(root + "/catalog/foreign/notes"), QByteArray("foreign"));
        QCOMPARE(R::load(root + "/config/registrations.json").value("registrations").toObject().value("example").toObject().value("revisionId").toString(), QString("rev2"));
    }
    void rollbackCuts_data() {
        QTest::addColumn<QString>("cut"); QTest::addColumn<bool>("identical");
        for (const auto &cut : {"prepared", "backingUp-before", "backingUp-after", "promoting-before", "promoting-after", "state", "profile", "recovery-unpromote", "recovery-restore", "recovery-files", "recovery-done", "recovery-archived"})
            for (bool identical : {false, true}) QTest::newRow(qPrintable(QString(cut) + (identical ? "-identical" : "-changed"))) << QString(cut) << identical;
    }
    void rollbackCuts() {
        QFETCH(QString, cut); QFETCH(bool, identical);
        const auto root = fresh(), pkg = package(root); const auto p = profile(root); initial(p, root, pkg);
        const auto oldCatalog = R::inventory(root + "/catalog"); const auto oldState = R::read(root + "/config/registrations.json"), oldMapping = R::read(root + "/config/python-environments.json"), oldProfile = R::read(root + "/local/installations.json");
        if (!identical) R::write(pkg + "/payload", "new");
        const auto journalPath = root + "/config/.artest-register-transaction.json";
        {
            R::Transaction tx(p, root + "/local"); tx.stage(pkg, {}, "rev2", "example", "cpp");
            auto j = R::load(journalPath); const auto candidate = j.value("candidate").toString(), backup = j.value("backup").toString();
            auto phase = [&](const QString &value) { j["phase"] = value; R::save(journalPath, j); };
            if (cut != "prepared") phase("backingUp");
            if (cut != "prepared" && cut != "backingUp-before") R::move(root + "/catalog", backup);
            if (cut.startsWith("promoting") || cut == "state" || cut == "profile" || cut.startsWith("recovery")) phase("promoting");
            if (cut == "promoting-after" || cut == "state" || cut == "profile" || cut.startsWith("recovery")) R::move(candidate, root + "/catalog");
            if (cut == "state" || cut == "profile" || cut.startsWith("recovery")) {
                phase("writingState"); const auto records = j.value("files").toObject();
                for (const auto &k : {"state", "mapping"}) { const auto r = records.value(k).toObject(); R::write(r.value("path").toString(), QByteArray::fromBase64(r.value("newBase64").toString().toLatin1())); }
            }
            if (cut == "profile" || cut.startsWith("recovery")) {
                phase("writingProfile"); const auto r = j.value("files").toObject().value("profile").toObject(); R::write(r.value("path").toString(), QByteArray::fromBase64(r.value("newBase64").toString().toLatin1()));
            }
            if (cut.startsWith("recovery")) {
                QJsonObject cursor{{"format", "ARTestDev.RegistrationRecovery.1"}, {"journalSha256", R::hash(R::read(journalPath))}, {"token", j.value("token")}, {"step", "unpromote"}};
                R::move(root + "/catalog", candidate);
                if (cut != "recovery-unpromote") { cursor["step"] = "restore"; R::move(backup, root + "/catalog"); }
                if (cut == "recovery-files" || cut == "recovery-done" || cut == "recovery-archived") {
                    cursor["step"] = "files"; const auto records = j.value("files").toObject();
                    for (const auto &k : {"state", "mapping", "profile"}) { const auto r = records.value(k).toObject(); R::write(r.value("path").toString(), QByteArray::fromBase64(r.value("previousBase64").toString().toLatin1())); }
                }
                if (cut == "recovery-done" || cut == "recovery-archived") cursor["step"] = "done";
                R::save(journalPath + ".recovery", cursor);
                if (cut == "recovery-archived") R::move(journalPath, journalPath + ".rolled-back." + j.value("token").toString());
            }
        }
        { R::Transaction tx(p, root + "/local"); tx.recover(); tx.recover(); }
        QCOMPARE(R::inventory(root + "/catalog"), oldCatalog); QCOMPARE(R::read(root + "/config/registrations.json"), oldState);
        QCOMPARE(R::read(root + "/config/python-environments.json"), oldMapping); QCOMPARE(R::read(root + "/local/installations.json"), oldProfile);
        QVERIFY(!QFileInfo::exists(journalPath));
    }
    void killedPublisher_data() {
        QTest::addColumn<bool>("promoted"); QTest::addColumn<bool>("identical"); QTest::addColumn<bool>("initialRegistration");
        for (bool promoted : {false, true}) for (bool identical : {false, true}) for (bool initial : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(promoted).arg(identical).arg(initial))) << promoted << identical << initial;
    }
    void killedPublisher() {
        QFETCH(bool, promoted); QFETCH(bool, identical); QFETCH(bool, initialRegistration);
        const auto root = fresh(), pkg = package(root); const auto p = profile(root);
        if (!initialRegistration) initial(p, root, pkg);
        const auto old = initialRegistration ? QJsonObject{} : R::inventory(root + "/catalog");
        const auto selected = initialRegistration ? QByteArray{} : R::read(root + "/local/installations.json");
        if (!identical) R::write(pkg + "/payload", "new");
        R::save(root + "/request.json", {{"root", root}, {"profile", p}, {"package", pkg}, {"promoted", promoted}});
        TreeProcess publisher; QEventLoop loop; ProcessResult result; bool observed = false;
        connect(&publisher, &TreeProcess::completed, &loop, [&](const ProcessResult &r) { result = r; if (!publisher.busy()) loop.quit(); });
        connect(&publisher, &TreeProcess::settled, &loop, &QEventLoop::quit);
        QTimer trigger; connect(&trigger, &QTimer::timeout, &loop, [&] { if (QFileInfo::exists(root + "/ready")) { observed = true; publisher.cancel(); } }); trigger.start(10);
        QVERIFY(publisher.start(QCoreApplication::applicationFilePath(), {"--transaction-probe", root + "/request.json"}, root, 30000)); loop.exec();
        QVERIFY(observed); QCOMPARE(result.status, ProcessResult::Status::Cancelled); QVERIFY(!publisher.busy());
        { R::Transaction tx(p, root + "/local"); tx.recover(); tx.recover(); }
        if (initialRegistration) { QVERIFY(!QFileInfo::exists(root + "/catalog")); QVERIFY(!QFileInfo::exists(root + "/local/installations.json")); }
        else { QCOMPARE(R::inventory(root + "/catalog"), old); QCOMPARE(R::read(root + "/local/installations.json"), selected); }
    }
    void foreignAndAmbiguous() {
        const auto root = fresh(), pkg = package(root); const auto p = profile(root);
        R::copy(pkg, root + "/catalog/foreign"); const auto old = R::inventory(root + "/catalog");
        { R::Transaction tx(p, root + "/local"); QVERIFY_THROWS_EXCEPTION(std::exception, tx.stage(pkg, {}, "rev1", "example", "cpp")); }
        QCOMPARE(R::inventory(root + "/catalog"), old);
        const auto other = fresh(), otherPkg = package(other); const auto p2 = profile(other); initial(p2, other, otherPkg);
        { R::Transaction tx(p2, other + "/local"); tx.stage(otherPkg, {}, "rev2", "example", "cpp"); }
        R::write(other + "/catalog/foreign-added", "preserve");
        { R::Transaction tx(p2, other + "/local"); QVERIFY_THROWS_EXCEPTION(std::exception, tx.recover()); }
        QCOMPARE(R::read(other + "/catalog/foreign-added"), QByteArray("preserve"));
    }
    void realIntegration_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("variant");
        for (const auto &language : {"cpp", "python"}) for (const auto &variant : {"driver-only", "command-only", "driver-command"}) QTest::newRow(qPrintable(QString(language) + '-' + variant)) << QString(language) << QString(variant);
    }
    void realIntegration() {
        QFETCH(QString, language); QFETCH(QString, variant);
        QVERIFY2(QFileInfo::exists(cli_) && QFileInfo::exists(sdk_ + "/ARTestDev.exe"), "Required real target/staging unavailable");
        const auto root = fresh(); auto target = R::manualProfile(cli_, root + "/local");
        CreateRequest request{language + '-' + variant, language, variant, root + "/projects", inspectStagingExecutable(sdk_ + "/ARTestDev.exe")};
        auto c = beginCreation(request); QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
        if (language == "python") { QVERIFY2(QFileInfo::exists(python_), "Required external Python unavailable"); auto r = process(python_, pythonCreateArguments(c), root + "/generate.txt"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData()); }
        c = finishCreation(c); QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n'))); const auto project = inspectProject(c.destination); QVERIFY(project.valid);
        auto build = [&] {
            return process(qEnvironmentVariable("ARTESTDEV_TEST_MSBUILD"), {project.projectFile, "/t:Build", "/m:1", "/nr:false", "/v:minimal", "/p:Configuration=" + config_ + ";Platform=x64"}, root + "/build-" + QUuid::createUuid().toString(QUuid::Id128) + ".txt");
        };
        if (language == "cpp") { const auto r = build(); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData()); }
        const auto cancelled = integrate(project, target, root + "/local", true); QVERIFY(!cancelled.success); QVERIFY(!QFileInfo::exists(root + "/local/installations.json"));
        QString otherId, otherPackage;
        QJsonObject otherInventory;
        if (variant == "command-only") {
            CreateRequest otherRequest{"other-driver", language, "driver-only", root + "/other-projects", request.kit};
            auto other = beginCreation(otherRequest); QVERIFY(other.success);
            if (language == "python") { const auto generated = process(python_, pythonCreateArguments(other), root + "/other-generate.txt"); QVERIFY2(generated.status == ProcessResult::Status::Success, generated.output.constData()); }
            other = finishCreation(other); QVERIFY(other.success); otherId = other.extensionId;
            const auto otherProject = inspectProject(other.destination);
            const auto source = other.destination + (language == "cpp" ? "/Extension.cpp" : "/src/extension.py");
            auto code = R::read(source); code.replace((other.extensionId + ".contract.simulated-source.v1").toUtf8(), "com.example.artest.contract.value-source.v1"); if (language == "python") code.replace("READ_OPERATION = CONTRACT + \"/read\"", "READ_OPERATION = \"com.example.artest.instrument.value-source.v1/read\""); R::write(source, code);
            if (language == "cpp") {
                const auto behavior = other.destination + "/SimulatedValueSource.h";
                auto header = R::read(behavior); header.replace((other.extensionId + ".contract.simulated-source.v1").toUtf8(), "com.example.artest.contract.value-source.v1"); header.replace("com.example.artest.contract.value-source.v1/read", "com.example.artest.instrument.value-source.v1/read"); R::write(behavior, header);
                const auto built = process(qEnvironmentVariable("ARTESTDEV_TEST_MSBUILD"), {otherProject.projectFile, "/t:Build", "/m:1", "/nr:false", "/v:minimal", "/p:Configuration=" + config_ + ";Platform=x64"}, root + "/other-build.txt");
                QVERIFY2(built.status == ProcessResult::Status::Success, built.output.constData());
            }
            const auto registered = integrate(otherProject, target, root + "/local"); QVERIFY2(registered.success, qPrintable(registered.diagnostic));
            otherPackage = target.value("catalog").toString() + "/registered-" + R::hash(otherId.toUtf8()).left(16); otherInventory = R::inventory(otherPackage);
        }
        const auto first = integrate(project, target, root + "/local"); QVERIFY2(first.success, qPrintable(first.diagnostic));
        const auto statePath = target.value("configuration").toString() + "/registrations.json", mappingPath = target.value("configuration").toString() + "/python-environments.json";
        const auto state = R::read(statePath), selected = R::read(root + "/local/installations.json");
        auto absent = target; absent["cli"] = root + "/absent/ARTestCLI.exe";
        QVERIFY(!integrate(project, absent, root + "/local").success); QCOMPARE(R::read(root + "/local/installations.json"), selected);
        QDir().mkpath(root + "/incompatible");
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath() + "/ARTestDevIncompatiblePython.exe", root + "/incompatible/ARTestCLI.exe"));
        R::write(root + "/incompatible/ARTestEngine.dll", "incompatible fixture");
        const auto incompatible = R::manualProfile(root + "/incompatible", root + "/local");
        QVERIFY(!integrate(project, incompatible, root + "/local").success); QCOMPARE(R::read(root + "/local/installations.json"), selected);
        const auto lockPath = root + "/local/.artest-installations.lock"; const auto permissions = QFile::permissions(lockPath);
        QVERIFY(QFile::setPermissions(lockPath, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther));
        const auto denied = integrate(project, target, root + "/local");
        QVERIFY(QFile::setPermissions(lockPath, permissions)); QVERIFY(!denied.success); QCOMPARE(R::read(root + "/local/installations.json"), selected);
        { R::Lock writer(root + "/local/.artest-installations.lock"); QVERIFY(!integrate(project, target, root + "/local").success); }
        QCOMPARE(R::read(root + "/local/installations.json"), selected);
        const auto repeat = integrate(project, target, root + "/local"); QVERIFY2(repeat.success, qPrintable(repeat.diagnostic)); QCOMPARE(repeat.revision, first.revision); QCOMPARE(R::read(statePath), state);
        if (language == "cpp") {
            const auto manifest = project.root + "/.artest/native/" + config_ + "/current/package/artest-extension.json";
            const auto bytes = R::read(manifest); R::write(manifest, bytes + "corrupt");
            const auto corrupt = integrate(project, target, root + "/local"); R::write(manifest, bytes);
            QVERIFY(!corrupt.success); QCOMPARE(R::read(statePath), state); QCOMPARE(R::read(root + "/local/installations.json"), selected);
        }
        const auto source = project.root + (language == "cpp" ? "/Extension.cpp" : "/src/extension.py");
        auto changed = R::read(source);
        changed.replace(language == "cpp" ? ".description = \"\"" : "description=\"\"", language == "cpp" ? ".description = \"Updated description\"" : "description=\"Updated description\"");
        R::write(source, changed + (language == "cpp" ? "\n// update\n" : "\n# update\n"));
        if (language == "cpp") { const auto stale = integrate(project, target, root + "/local"); QVERIFY(!stale.success); QCOMPARE(R::read(statePath), state); QCOMPARE(R::read(root + "/local/installations.json"), selected); const auto r = build(); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData()); }
        const auto updated = integrate(project, target, root + "/local"); QVERIFY2(updated.success, qPrintable(updated.diagnostic));
        QVERIFY(updated.revision != first.revision);
        if (!otherPackage.isEmpty()) {
            QCOMPARE(R::inventory(otherPackage), otherInventory);
            auto plan = R::load(project.plan); auto instruments = plan.value("instruments").toArray(); QVERIFY(!instruments.isEmpty());
            auto instrument = instruments.at(0).toObject();
            const auto driver = R::load(otherPackage + "/artest-extension.json").value("components").toArray().at(0).toObject();
            QCOMPARE(driver.value("contractId").toString(), QString("com.example.artest.contract.value-source.v1"));
            if (language == "python") instrument["config"] = QJsonObject{{"value", 42.0}};
            instrument["type"] = driver.value("typeId"); instruments[0] = instrument; plan["instruments"] = instruments;
            R::save(root + "/external-driver-plan.json", plan);
            const auto broker = process(cli_, {"compile", root + "/external-driver-plan.json", "--extensions", target.value("catalog").toString(), "--python-environments", mappingPath}, root + "/external-driver-compile.txt");
            QVERIFY2(broker.status == ProcessResult::Status::Success, broker.output.constData());
        }
        R::move(project.root, project.root + "-unavailable");
        const auto validate = process(cli_, {"extensions", "validate", target.value("catalog").toString()}, root + "/independent.txt"); QVERIFY2(validate.status == ProcessResult::Status::Success, validate.output.constData());
        const auto independencePlan = root + "/independence.json";
        R::save(independencePlan, {{"format", "ARTest.Script"}, {"version", 1}, {"instruments", QJsonArray{}}, {"commands", QJsonArray{QJsonObject{{"stepId", 1}, {"name", "Time.WaitMs"}, {"instrument", "NoInstrument"}, {"params", QJsonObject{{"milliseconds", 0}}}}}}});
        const auto compiled = process(cli_, {"compile", independencePlan, "--extensions", target.value("catalog").toString(), "--python-environments", mappingPath}, root + "/independent-compile.txt");
        QVERIFY2(compiled.status == ProcessResult::Status::Success, compiled.output.constData());
        if (language == "python") { const auto receipt = R::load(mappingPath).value(project.extensionId).toString(); QVERIFY(R::absolute(receipt).startsWith(R::absolute(target.value("configuration").toString()) + '/')); QVERIFY(QFileInfo::exists(receipt)); }
        if (variant == "driver-command") cancellationRequests_.append(lastRequest_);
    }
    void cancelPublication() {
        if (cancellationRequests_.isEmpty()) {
            const auto file = qEnvironmentVariable("ARTESTDEV_CANCELLATION_REQUESTS"); QVERIFY2(QFileInfo::exists(file), "Run realIntegration first or provide retained requests");
            for (const auto &value : R::load(file).value("requests").toArray()) cancellationRequests_.append(value.toString());
        }
        QCOMPARE(cancellationRequests_.size(), 2);
        for (const auto &requestPath : cancellationRequests_) {
            auto request = R::load(requestPath); const auto path = R::absolute(request.value("project").toString());
            const auto fixture = QFileInfo(QFileInfo(requestPath).absolutePath()).absolutePath();
            QVERIFY(fixture.contains("/ARTestDev015-")); QVERIFY(path.startsWith(fixture + "/projects/"));
            QVERIFY(!QFileInfo::exists(path)); QVERIFY(QFileInfo::exists(path + "-unavailable")); R::move(path + "-unavailable", path);
            const auto target = request.value("target").toObject(), before = R::inventory(target.value("catalog").toString());
            const auto config = target.value("configuration").toString(), registry = request.value("registryRoot").toString() + "/installations.json";
            const auto state = R::read(config + "/registrations.json"), mapping = R::read(config + "/python-environments.json"), selected = R::read(registry);
            if (request.value("python").toString().isEmpty()) {
                const auto current = path + "/.artest/native/" + request.value("configuration").toString() + "/current";
                R::move(current, current + "-missing-output-fixture");
                const auto missing = process(QCoreApplication::applicationFilePath(), {"--integration-probe", requestPath}, root_ + "/missing-native-output.txt");
                R::move(current + "-missing-output-fixture", current);
                QVERIFY2(missing.status == ProcessResult::Status::Success, qPrintable(missing.detail));
                QVERIFY(missing.output.contains("Build requerido"));
                const auto marker = missing.output.lastIndexOf("ARTESTDEV_INTEGRATION="); QVERIFY(marker >= 0);
                QVERIFY(!R::object(missing.output.mid(marker + QByteArray("ARTESTDEV_INTEGRATION=").size()).trimmed()).value("success").toBool());
                QCOMPARE(R::inventory(target.value("catalog").toString()), before); QCOMPARE(R::read(registry), selected);
                QCOMPARE(R::read(config + "/registrations.json"), state); QCOMPARE(R::read(config + "/python-environments.json"), mapping);
            }
            request["cancel"] = false; request["cancelAfterProgress"] = "Publicando";
            const auto cancellation = fixture + "/cancel-" + QUuid::createUuid().toString(QUuid::Id128) + ".json"; R::save(cancellation, request);
            const auto result = process(QCoreApplication::applicationFilePath(), {"--integration-probe", cancellation}, root_ + "/cancel-" + QFileInfo(path).fileName() + ".txt");
            QVERIFY2(result.status == ProcessResult::Status::Success, qPrintable(result.detail + QString::fromUtf8(result.output)));
            QVERIFY(result.output.contains("Publicando y confirmando"));
            const auto marker = result.output.lastIndexOf("ARTESTDEV_INTEGRATION="); QVERIFY(marker >= 0);
            QVERIFY(!R::object(result.output.mid(marker + QByteArray("ARTESTDEV_INTEGRATION=").size()).trimmed()).value("success").toBool());
            QCOMPARE(R::inventory(target.value("catalog").toString()), before); QCOMPARE(R::read(config + "/registrations.json"), state);
            QCOMPARE(R::read(config + "/python-environments.json"), mapping); QCOMPARE(R::read(registry), selected);
            R::move(path, path + "-unavailable");
        }
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args.at(1) == "--transaction-probe") {
        const auto request = R::load(args.at(2)); const auto root = request.value("root").toString();
        R::Transaction tx(request.value("profile").toObject(), root + "/local"); tx.recover();
        tx.stage(request.value("package").toString(), {}, "rev2", "example", "cpp");
        if (request.value("promoted").toBool()) tx.promote();
        R::write(root + "/ready", "publisher owns locks and awaits interruption");
        QTimer::singleShot(30000, &app, &QCoreApplication::quit); return app.exec();
    }
    if (args.size() == 3 && args.at(1) == "--integration-probe") {
        const auto input = R::load(args.at(2)); IntegrationRequest r;
        r.project = inspectProject(input.value("project").toString()); r.target = input.value("target").toObject(); r.registryRoot = input.value("registryRoot").toString();
        r.python = input.value("python").toString(); r.configuration = input.value("configuration").toString(); r.stagedExecutable = input.value("stagedExecutable").toString();
        IntegrationService service;
        QObject::connect(&service, &IntegrationService::progress, &app, [&](const QString &message) {
            std::cout << message.toUtf8().constData() << std::endl;
            const auto cut = input.value("cancelAfterProgress").toString(); if (!cut.isEmpty() && message.startsWith(cut)) service.cancel();
        });
        QObject::connect(&service, &IntegrationService::completed, &app, [&](const IntegrationResult &result) {
            std::cout << "ARTESTDEV_INTEGRATION=" << QJsonDocument(QJsonObject{{"success", result.success}, {"target", result.target}, {"revision", result.revision}, {"diagnostic", result.diagnostic}}).toJson(QJsonDocument::Compact).constData() << std::endl;
            app.exit(0);
        });
        service.start(r); if (input.value("cancel").toBool()) service.cancel(); return app.exec();
    }
    IntegrationTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "IntegrationTests.moc"

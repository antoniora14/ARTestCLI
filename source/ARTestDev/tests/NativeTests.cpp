#include "Authoring.h"
#include "SdkLocation.h"
#include "NativeOutputs.h"
#include "NativeRetention.h"
#include "TreeProcess.h"
#include "NativeService.h"
#include <vector>
#include <cstring>
#include <stdexcept>
#include <QCoreApplication>
#include <QDirIterator>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QScopeGuard>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winioctl.h>
using namespace ARTestDev;
namespace {
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

void copy(const QString &from, const QString &to) {
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next(), d = to + '/' + QDir(from).relativeFilePath(f);
        QDir().mkpath(QFileInfo(d).absolutePath());
        if (!QFile::copy(f, d)) qFatal("Fixture copy failed");
    }
}
QByteArray bytes(const QString &p) { QFile f(p); if (!f.open(QIODevice::ReadOnly)) qFatal("read"); return f.readAll(); }
void put(const QString &p, const QByteArray &b) { QFile f(p); if (!f.open(QIODevice::WriteOnly) || f.write(b) != b.size()) qFatal("write"); }
}
class NativeTests : public QObject {
    Q_OBJECT
    QString root_, sdk_, cli_, msbuild_, configuration_;
    ProcessResult run(const QString &exe, const QStringList &args, const QString &cwd, const QString &log) {
        TreeProcess p; QEventLoop loop; ProcessResult result; QByteArray audit;
        quint64 expectedProcesses = 0, observedProcesses = 0;
        p.enableProcessAudit();
        connect(&p, &TreeProcess::processAuditFinished, &loop, [&](quint64 n) { expectedProcesses = n; });
        connect(&p, &TreeProcess::processObserved, &loop, [&](quint64 id, const QString &image) {
            ++observedProcesses;
            audit += QByteArray::number(id) + " " + image.toUtf8() + '\n';
        });
        connect(&p, &TreeProcess::completed, &loop, [&](const ProcessResult &r) { result = r; if (!p.busy()) loop.quit(); });
        connect(&p, &TreeProcess::settled, &loop, &QEventLoop::quit);
        if (!p.start(exe, args, cwd, 300000, 1024 * 1024)) qFatal("start");
        loop.exec(); QCoreApplication::sendPostedEvents(&loop, QEvent::MetaCall);
        audit += "observed=" + QByteArray::number(observedProcesses) + " expected=" + QByteArray::number(expectedProcesses) + '\n';
        put(root_ + '/' + log, result.output); put(root_ + '/' + log + ".processes", audit);
        const auto lower = audit.toLower();
        if (!expectedProcesses || observedProcesses != expectedProcesses || lower.contains("powershell") || lower.contains("pwsh") || lower.contains("python") || lower.contains("unresolved")) {
            result.status = ProcessResult::Status::Failed;
            result.output += "\nForbidden or unresolved descendant in process audit:\n" + audit;
        }
        return result;
    }
    QJsonObject check(const QString &project, bool validate = false) {
        QStringList a{validate ? "validate-project" : "check-project", project, configuration_, sdk_ + "/native-sdk"};
        if (validate) a << cli_;
        const auto r = run(sdk_ + "/ARTestDevNative.exe", a, root_, "check-" + QUuid::createUuid().toString(QUuid::Id128) + ".log");
        return QJsonDocument::fromJson(r.output).object();
    }
private slots:
    void lateBoundArguments() {
        CreateRequest request; request.kit = inspectStagingExecutable(sdk_ + "/ARTestDev.exe");
        request.workspace = root_ + "/Late property projects"; request.name = "latebound"; request.language = "cpp"; request.variant = "driver-command";
        auto c = beginCreation(request); QVERIFY(c.success); c = finishCreation(c); QVERIFY(c.success);
        const QString project = c.destination + '/' + c.projectFile, state = c.destination + "/.artest/native/" + configuration_;
        auto xml = bytes(project); xml.replace("</Project>", "<Import Project=\"Late.props\" /></Project>"); put(project, xml);
        xml.replace("<ClCompile Include=\"Extension.cpp\" />", "<ClCompile Include=\"Extension.cpp\" /><ClCompile Include=\"Independent.cpp\" />"); put(project, xml);
        put(c.destination + "/Independent.cpp", "int independent_fixture() { return 42; }\n");
        put(c.destination + "/Late.props", "<Project><PropertyGroup><TargetName>RenamedOutput</TargetName></PropertyGroup></Project>");
        const QString source = c.destination + "/Extension.cpp", header = c.destination + "/Name.h";
        auto definition = bytes(source); definition.replace("\"Example\"", "FIXTURE_PUBLISHER"); put(source, "#include \"Name.h\"\n" + definition);
        put(header, "#define FIXTURE_PUBLISHER \"Example\"\n");
        const auto build = [&](const QString &target, const QString &label) {
            return run(msbuild_, {project, "/t:" + target, "/m:1", "/nr:false", "/v:minimal", "/p:Configuration=" + configuration_ + ";Platform=x64"}, root_, "late-" + label + ".log");
        };
        auto r = build("Build", "first"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        const auto manifest = [&] { return Native::readObject(state + "/current/package/artest-extension.json"); };
        QCOMPARE(manifest().value("runtime").toObject().value("entry").toString(), QString("RenamedOutput.dll"));
        const auto exactEdit = [&](const QString &path, const QByteArray &replacement, const QString &label) {
            if (bytes(path).size() != replacement.size()) return false;
            const auto native = QDir::toNativeSeparators(path);
            HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return false;
            FILETIME before{}; const bool read = GetFileTime(file, nullptr, nullptr, &before); CloseHandle(file);
            if (!read) return false;
            put(path, replacement);
            file = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return false;
            FILETIME after{}; const bool restored = SetFileTime(file, nullptr, nullptr, &before) && GetFileTime(file, nullptr, nullptr, &after); CloseHandle(file);
            Native::writeObject(root_ + "/exact-time-" + label + ".json", {{"path", path}, {"bytes", replacement.size()},
                {"before100ns", QString::number((quint64(before.dwHighDateTime) << 32) | before.dwLowDateTime)},
                {"after100ns", QString::number((quint64(after.dwHighDateTime) << 32) | after.dwLowDateTime)}});
            return restored && before.dwHighDateTime == after.dwHighDateTime && before.dwLowDateTime == after.dwLowDateTime;
        };
        for (const auto &label : QStringList{"source", "header"}) {
            const QString path = label == "header" ? header : source;
            auto content = bytes(path);
            if (label == "header") content.replace("Example", "ExamplE");
            else content.replace("latebound extension", "latebound Extension");
            const auto dllObject = Native::digest(state + "/intermediates/obj/Extension.obj");
            const auto metadataObject = Native::digest(state + "/intermediates/metaobj/Extension.obj");
            // Denying writes/deletion proves the independent objects survive, even if
            // recompiling an unchanged source would produce identical object bytes.
            std::vector<HANDLE> independentLocks;
            QJsonObject independentBefore;
            const auto unlock = qScopeGuard([&] { for (HANDLE handle : independentLocks) CloseHandle(handle); });
            for (const auto &directory : QStringList{"obj", "metaobj"}) {
                const QString object = state + "/intermediates/" + directory + "/Independent.obj";
                independentBefore[directory] = Native::digest(object);
                HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(object).utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                QVERIFY(handle != INVALID_HANDLE_VALUE); independentLocks.push_back(handle);
            }
            QVERIFY(exactEdit(path, content, label));
            r = build("Build", label); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
            QVERIFY(Native::digest(state + "/intermediates/obj/Extension.obj") != dllObject);
            QVERIFY(Native::digest(state + "/intermediates/metaobj/Extension.obj") != metadataObject);
            QVERIFY(!Native::readObject(state + "/last-success.json").value("result").toObject().value("metrics").toObject().value("compacted").toBool());
            for (const auto &directory : QStringList{"obj", "metaobj"})
                QCOMPARE(Native::digest(state + "/intermediates/" + directory + "/Independent.obj"), independentBefore[directory].toString());
            Native::writeObject(root_ + "/selective-" + label + ".json", {{"independentObjectsWriteLocked", true}, {"independentHashes", independentBefore}, {"compacted", false}});
        }
        QCOMPARE(manifest().value("publisher").toString(), QString("ExamplE"));
        QCOMPARE(manifest().value("displayName").toString(), QString("latebound Extension"));
        QMap<QString, QDateTime> objectTimes;
        for (const auto &directory : QStringList{"obj", "metaobj"})
            for (const auto &name : QStringList{"Extension.obj", "Independent.obj"}) {
                const QString object = state + "/intermediates/" + directory + '/' + name;
                objectTimes[object] = QFileInfo(object).lastModified();
            }
        r = build("Rebuild", "selective-rebuild"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        for (auto it = objectTimes.cbegin(); it != objectTimes.cend(); ++it)
            QVERIFY(QFileInfo(it.key()).lastModified() != it.value());
        Native::writeObject(root_ + "/selective-rebuild.json", {{"rebuiltDllAndMetadataObjects", 4}});
        const auto published = Native::inventory(state + "/current");
        r = build("Clean", "clean"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QCOMPARE(Native::inventory(state + "/current"), published);
        r = build("Rebuild", "rebuild"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QCOMPARE(manifest().value("runtime").toObject().value("entry").toString(), QString("RenamedOutput.dll"));
        QCOMPARE(check(project, true).value("status").toString(), QString("target-validated"));
    }
    void inputSnapshot() {
        const QString state = root_ + "/snapshot", project = root_ + "/snapshot.vcxproj", marker = root_ + "/snapshot-marker";
        QVERIFY(QDir().mkpath(state)); put(project, "content A");
        qputenv("ARTESTDEV_FAULT_PHASE", "input-snapshot"); qputenv("ARTESTDEV_FAULT_MARKER", marker.toUtf8());
        const auto reset = qScopeGuard([] { qunsetenv("ARTESTDEV_FAULT_PHASE"); qunsetenv("ARTESTDEV_FAULT_MARKER"); });
        TreeProcess process; QEventLoop loop; QTimer poll; bool reached = false, denied = false;
        connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &) { if (!process.busy()) loop.quit(); });
        connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
        connect(&poll, &QTimer::timeout, &loop, [&] {
            if (!QFileInfo::exists(marker) || reached) return;
            reached = true; QFile writer(project);
            denied = !writer.open(QIODevice::WriteOnly) && !QFile::rename(project, project + ".moved");
            process.cancel();
        });
        QVERIFY(process.start(QCoreApplication::applicationDirPath() + "/ARTestDevPublicationProbe.exe", {state, project, "input-snapshot"}, root_, 20000));
        poll.start(20); loop.exec(); poll.stop();
        QVERIFY(reached); QVERIFY(denied); QCOMPARE(bytes(project), QByteArray("content A"));
        put(project, "content B"); QCOMPARE(bytes(project), QByteArray("content B"));
    }
    void retirementInterruption() {
        const QString state = root_ + "/retirement", project = root_ + "/fixture.vcxproj", scratch = state + "/intermediates";
        QVERIFY(QDir().mkpath(scratch + "/nested"));
        put(scratch + "/nested/first", "owned first"); put(scratch + "/second", "owned second");
        Native::sealScratch(scratch, project);
        QVERIFY(QDir().mkpath(state + "/current")); put(state + "/current/same", "published");
        Native::writeObject(state + "/current/ownership.json", {{"format", "ARTestDev.NativeOutput.1"}, {"project", project}, {"revision", "old"}, {"files", Native::inventory(state + "/current")}});
        const auto published = Native::inventory(state + "/current");
        const QString marker = root_ + "/retirement-marker";
        qputenv("ARTESTDEV_FAULT_PHASE", "retirement-delete"); qputenv("ARTESTDEV_FAULT_MARKER", marker.toUtf8());
        const auto reset = qScopeGuard([] { qunsetenv("ARTESTDEV_FAULT_PHASE"); qunsetenv("ARTESTDEV_FAULT_MARKER"); });
        TreeProcess process; QEventLoop loop; QTimer poll; bool reached = false; ProcessResult result;
        connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &r) { result = r; if (!process.busy()) loop.quit(); });
        connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
        connect(&poll, &QTimer::timeout, &loop, [&] { if (QFileInfo::exists(marker)) { reached = true; process.cancel(); } });
        QVERIFY(process.start(QCoreApplication::applicationDirPath() + "/ARTestDevPublicationProbe.exe", {state, project, "retire"}, root_, 20000));
        poll.start(20); loop.exec(); poll.stop();
        QVERIFY(reached); QCOMPARE(result.status, ProcessResult::Status::Cancelled); QVERIFY(!process.busy());
        qunsetenv("ARTESTDEV_FAULT_PHASE");
        QVERIFY(QFileInfo::exists(state + "/retirement.json"));
        const QString retirement = state + "/retirement.json";
        const auto retirementBytes = bytes(retirement);
        put(retirement, retirementBytes + " ");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Native::resumeRetirement(state, project));
        QCOMPARE(bytes(retirement), retirementBytes + " "); put(retirement, retirementBytes);
        put(state + "/transaction.json", "{}");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Native::resumeRetirement(state, project));
        QVERIFY(QFile::remove(state + "/transaction.json"));
        const QString moved = root_ + "/retirement-original";
        QVERIFY(QDir().rename(scratch, moved)); copy(moved, scratch);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Native::resumeRetirement(state, project));
        // This is the exact test-created impostor copy, never production cleanup.
        QVERIFY(QDir(scratch).removeRecursively()); QVERIFY(QDir().rename(moved, scratch));
        put(scratch + "/unknown", "foreign");
        const auto before = Native::inventory(state);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Native::resumeRetirement(state, project));
        QCOMPARE(Native::inventory(state), before); QVERIFY(QFile::remove(scratch + "/unknown"));
        put(scratch + "/second", "changed");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Native::resumeRetirement(state, project));
        put(scratch + "/second", "owned second");
        QVERIFY(makeJunction(scratch + "/link", sdk_));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Native::resumeRetirement(state, project));
        QVERIFY(RemoveDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(scratch + "/link").utf16())));
        HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(scratch + "/second").utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(locked != INVALID_HANDLE_VALUE);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Native::resumeRetirement(state, project));
        CloseHandle(locked);
        Native::resumeRetirement(state, project); Native::resumeRetirement(state, project);
        QVERIFY(!QFileInfo::exists(scratch)); QVERIFY(!QFileInfo::exists(state + "/retirement.json"));
        QCOMPARE(Native::inventory(state + "/current"), published);
        const QString revision = QUuid::createUuid().toString(QUuid::Id128), assembled = state + "/work-" + revision + "/assembled";
        QVERIFY(QDir().mkpath(assembled)); put(assembled + "/same", "published");
        Native::writeObject(assembled + "/ownership.json", {{"format", "ARTestDev.NativeOutput.1"}, {"project", project}, {"revision", revision}, {"files", Native::inventory(assembled)}});
        Native::publishFixture(state, project, revision);
        QCOMPARE(Native::readObject(state + "/current/ownership.json").value("revision").toString(), revision);
    }
    void incrementalRetention() {
        CreateRequest request; request.kit = inspectStagingExecutable(sdk_ + "/ARTestDev.exe");
        request.workspace = root_ + "/Incremental projects"; request.name = "incremental"; request.language = "cpp"; request.variant = "driver-command";
        auto c = beginCreation(request); QVERIFY(c.success); c = finishCreation(c); QVERIFY(c.success);
        const QString project = c.destination + '/' + c.projectFile, state = c.destination + "/.artest/native/" + configuration_;
        const QString source = c.destination + "/Extension.cpp";
        auto xml = bytes(project); xml.replace("<ClCompile Include=\"Extension.cpp\" />", "<ClCompile Include=\"Extension.cpp\" /><ClCompile Include=\"Independent.cpp\" />"); put(project, xml);
        put(c.destination + "/Independent.cpp", "int independent_fixture() { return 42; }\n");
        const auto build = [&](const QString &target, const QString &label) {
            return run(msbuild_, {project, "/t:" + target, "/m:1", "/nr:false", "/v:minimal", "/p:Configuration=" + configuration_ + ";Platform=x64"}, root_, "incremental-" + label + ".log");
        };
        auto r = build("Build", "first"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        const auto first = Native::readObject(state + "/current/ownership.json");
        QJsonArray storage;
        const auto count = [&](const QString &label) {
            qint64 files = 0, size = 0, directories = 0;
            QDirIterator it(state, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) { it.next(); if (it.fileInfo().isDir()) ++directories; else { ++files; size += it.fileInfo().size(); } }
            storage.append(QJsonObject{{"label", label}, {"files", files}, {"bytes", size}, {"directories", directories}});
            Native::writeObject(root_ + "/storage.json", {{"configuration", configuration_}, {"samples", storage}});
            return files;
        };
        const auto initialFiles = count("first");
        const auto initialBytes = storage.last().toObject().value("bytes").toInteger();
        for (int i = 0; i < 20; ++i) {
            const QString label = "unchanged-" + QString::number(i);
            r = build("Build", label); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
            QCOMPARE(Native::readObject(state + "/current/ownership.json"), first);
            const auto audit = bytes(root_ + "/incremental-" + label + ".log.processes").toLower();
            QVERIFY(!audit.contains("cl.exe") && !audit.contains("link.exe") && !audit.contains("incrementalmetadata.exe"));
            QCOMPARE(count(label), initialFiles);
        }
        const auto original = bytes(source);
        const QString independent = state + "/intermediates/obj/Independent.obj";
        QVERIFY(QFileInfo::exists(independent));
        int compactions = 0;
        for (int i = 0; i < 20; ++i) {
            QMap<QString, QPair<QString, QDateTime>> independentObjects;
            for (const auto &directory : QStringList{"obj", "metaobj"}) {
                const QString object = state + "/intermediates/" + directory + "/Independent.obj";
                independentObjects[object] = {Native::digest(object), QFileInfo(object).lastModified()};
            }
            const auto timestamp = QFileInfo(source).lastModified();
            auto text = original; text.replace("incremental extension", QByteArray("incremental extensio") + char('A' + i));
            QCOMPARE(text.size(), original.size()); put(source, text);
            QFile edited(source); QVERIFY(edited.open(QIODevice::ReadWrite)); QVERIFY(edited.setFileTime(timestamp, QFileDevice::FileModificationTime)); edited.close();
            r = build("Build", "edit-" + QString::number(i)); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
            const auto manifest = bytes(state + "/current/package/artest-extension.json");
            QVERIFY(manifest.contains(QByteArray("incremental extensio") + char('A' + i)));
            QCOMPARE(count("edit-" + QString::number(i)), initialFiles);
            QVERIFY(storage.last().toObject().value("bytes").toInteger() <= initialBytes + qMax(8LL * 1024 * 1024, initialBytes / 2) + 1024 * 1024);
            if (Native::readObject(state + "/last-success.json").value("result").toObject().value("metrics").toObject().value("compacted").toBool()) ++compactions;
            else for (auto it = independentObjects.cbegin(); it != independentObjects.cend(); ++it) {
                QCOMPARE(Native::digest(it.key()), it.value().first);
                QCOMPARE(QFileInfo(it.key()).lastModified(), it.value().second);
            }
        }
        QVERIFY(compactions > 0);
        const auto current = Native::readObject(state + "/current/ownership.json");
        put(c.destination + "/Notes.md", "not a native dependency");
        put(c.destination + "/TestPlan.json", "not compiled");
        r = build("Build", "documents"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QCOMPARE(Native::readObject(state + "/current/ownership.json"), current);
        r = build("Clean", "clean"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QVERIFY(!QFileInfo::exists(state + "/intermediates")); QCOMPARE(Native::readObject(state + "/current/ownership.json"), current);
        r = build("Build", "after-clean"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QVERIFY(QFileInfo::exists(independent));
        count("after-clean");
        auto definition = original;
        definition.replace("\"incremental extension\"", "FIXTURE_LABEL");
        put(source, "#include \"Label.h\"\n" + definition);
        const QString label = c.destination + "/Label.h";
        put(label, "#define FIXTURE_LABEL \"header-A\"\n");
        r = build("Build", "header-added"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        const auto headerTime = QFileInfo(label).lastModified();
        put(label, "#define FIXTURE_LABEL \"header-B\"\n");
        { QFile f(label); QVERIFY(f.open(QIODevice::ReadWrite)); QVERIFY(f.setFileTime(headerTime, QFileDevice::FileModificationTime)); }
        r = build("Build", "header-content"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QVERIFY(bytes(state + "/current/package/artest-extension.json").contains("header-B"));
        QVERIFY(QFile::rename(label, c.destination + "/Label.txt"));
        put(source, "#include \"Label.txt\"\n" + definition);
        r = build("Build", "dependency-renamed"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        const QString data = c.destination + "/Label.txt";
        const auto dataTime = QFileInfo(data).lastModified(); put(data, "#define FIXTURE_LABEL \"header-C\"\n");
        { QFile f(data); QVERIFY(f.open(QIODevice::ReadWrite)); QVERIFY(f.setFileTime(dataTime, QFileDevice::FileModificationTime)); }
        r = build("Build", "text-include"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QVERIFY(bytes(state + "/current/package/artest-extension.json").contains("header-C"));
        xml.replace("</Project>", "<Import Project=\"Options.props\" /><ItemGroup><ARTestDevInput Include=\"Notes.md\" /></ItemGroup></Project>"); put(project, xml);
        const QString props = c.destination + "/Options.props";
        put(props, "<Project><ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>FIXTURE_OPTION=1;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup></Project>");
        r = build("Build", "import-added"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        const auto propsTime = QFileInfo(props).lastModified(); auto options = bytes(props); options.replace("OPTION=1", "OPTION=2"); put(props, options);
        { QFile f(props); QVERIFY(f.open(QIODevice::ReadWrite)); QVERIFY(f.setFileTime(propsTime, QFileDevice::FileModificationTime)); }
        r = build("Build", "import-content"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QVERIFY(bytes(root_ + "/incremental-import-content.log.processes").toLower().contains("cl.exe"));
        const auto priorDeclared = Native::readObject(state + "/current/ownership.json");
        put(c.destination + "/Notes.md", "declared native dependency changed");
        r = build("Build", "declared-document"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QVERIFY(Native::readObject(state + "/current/ownership.json") != priorDeclared);
        QVERIFY(bytes(root_ + "/incremental-declared-document.log.processes").toLower().contains("cl.exe"));
        const auto beforeRebuild = QFileInfo(independent).lastModified();
        r = build("Rebuild", "explicit-rebuild"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        QVERIFY(QFileInfo(independent).lastModified() != beforeRebuild);
        const auto pristine = Native::inventory(state + "/current");
        put(state + "/intermediates/unknown.txt", "owner material");
        r = build("Build", "unknown-cache"); QVERIFY(r.status != ProcessResult::Status::Success);
        QCOMPARE(Native::inventory(state + "/current"), pristine);
        QVERIFY(QFile::remove(state + "/intermediates/unknown.txt"));
        r = build("Build", "recovered-cache"); QVERIFY2(r.status == ProcessResult::Status::Success, r.output.constData());
        count("dependencies-complete");
    }
    void benchmark() {
        QJsonArray samples;
        try {
        for (int sample = 0; sample < 3; ++sample) {
            CreateRequest request; request.kit = inspectStagingExecutable(sdk_ + "/ARTestDev.exe");
            request.workspace = root_ + "/Bench projects"; request.name = "bench" + QString::number(sample);
            request.language = "cpp"; request.variant = "driver-command";
            auto c = beginCreation(request); QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
            c = finishCreation(c); QVERIFY(c.success);
            const QString project = c.destination + '/' + c.projectFile;
            for (const QString &scenario : QStringList{"first", "unchanged", "edit", "rebuild"}) {
                if (scenario == "edit") {
                    const QString source = c.destination + "/Extension.cpp";
                    auto content = bytes(source); content.replace(request.name.toUtf8() + " extension", request.name.toUtf8() + " Extension"); put(source, content);
                }
                QElapsedTimer timer; timer.start();
                const auto result = run(msbuild_, {project, scenario == "rebuild" ? "/t:Rebuild" : "/t:Build", "/m:1", "/nr:false", "/v:minimal", "/p:Configuration=" + configuration_ + ";Platform=x64"}, root_, QString::number(sample) + '-' + scenario + ".log");
                const auto elapsed = timer.elapsed();
                QVERIFY2(result.status == ProcessResult::Status::Success, result.output.constData());
                QJsonObject detail;
                for (const auto &line : result.output.split('\n')) {
                    const int offset = line.indexOf("{\"inputIdentity\"");
                    if (offset >= 0) detail = QJsonDocument::fromJson(line.mid(offset).trimmed()).object();
                }
                qint64 count = 0, size = 0;
                QDirIterator files(c.destination + "/.artest/native", QDir::Files, QDirIterator::Subdirectories);
                while (files.hasNext()) { files.next(); ++count; size += files.fileInfo().size(); }
                samples.append(QJsonObject{{"sample", sample}, {"scenario", scenario}, {"elapsedMs", elapsed}, {"files", count}, {"bytes", size}, {"result", detail}});
                qInfo().noquote() << scenario << sample << elapsed << "ms";
            }
        }
        Native::writeObject(root_ + "/benchmark.json", {{"configuration", configuration_}, {"samples", samples}});
        } catch (const std::exception &error) { QFAIL(error.what()); }
    }
    void initTestCase() {
        const QString staging = qEnvironmentVariable("ARTESTDEV_TEST_STAGING"), target = qEnvironmentVariable("ARTESTDEV_TEST_CLI");
        QVERIFY2(!staging.isEmpty() && QFileInfo::exists(target), "Explicit staging and separate CLI required; no acceptance skip");
        QTemporaryDir d(QDir::tempPath() + "/dev014-XXXXXX"); QVERIFY(d.isValid()); d.setAutoRemove(false); root_ = d.path();
        sdk_ = root_ + "/SDK with spaces"; copy(staging, sdk_);
        const auto kit = inspectStagingExecutable(sdk_ + "/ARTestDev.exe"); QVERIFY2(kit.valid, qPrintable(kit.diagnostics.join('\n')));
        configuration_ = Native::readObject(sdk_ + "/artestdev-staging.json").value("configuration").toString();
        const QString runtime = root_ + "/Separate CLI"; QDir().mkdir(runtime);
        const QDir origin(QFileInfo(target).absolutePath());
        for (const QString &f : origin.entryList({"*.dll", "ARTestCLI.exe"}, QDir::Files)) QVERIFY(QFile::copy(origin.filePath(f), runtime + '/' + f));
        cli_ = runtime + "/ARTestCLI.exe";
        msbuild_ = qEnvironmentVariable("ARTESTDEV_TEST_MSBUILD"); QVERIFY(QFileInfo::exists(msbuild_));
        qInfo().noquote() << "Retained DEV-01.4 evidence:" << root_;
    }
    void variants_data() {
        QTest::addColumn<QString>("variant");
        for (const char *v : {"driver-only", "command-only", "driver-command"}) QTest::newRow(v) << QString::fromLatin1(v);
    }
    void variants() {
        QFETCH(QString, variant);
        const auto kit = inspectStagingExecutable(sdk_ + "/ARTestDev.exe");
        CreateRequest request; request.kit = kit; request.workspace = root_ + "/Projects with spaces"; request.name = variant; request.language = "cpp"; request.variant = variant;
        auto c = beginCreation(request); QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
        c = finishCreation(c); QVERIFY2(c.success, qPrintable(c.diagnostics.join('\n')));
        const QString project = c.destination + '/' + c.projectFile;
        QCOMPARE(check(project).value("status").toString(), "missing");
        const auto beforeSdk = Native::inventory(sdk_);
        PACL oldAcl = nullptr; PSECURITY_DESCRIPTOR oldDescriptor = nullptr, readOnlyDescriptor = nullptr;
        auto *native = reinterpret_cast<LPWSTR>(const_cast<ushort *>(sdk_.utf16()));
        QCOMPARE(GetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &oldDescriptor), DWORD(ERROR_SUCCESS));
        QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;GRGX;;;WD)", SDDL_REVISION_1, &readOnlyDescriptor, nullptr));
        PACL readOnlyAcl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        QVERIFY(GetSecurityDescriptorDacl(readOnlyDescriptor, &present, &readOnlyAcl, &defaulted));
        const auto restoreAcl = qScopeGuard([&] {
            SetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, oldAcl, nullptr);
            LocalFree(oldDescriptor); LocalFree(readOnlyDescriptor);
        });
        QCOMPARE(SetNamedSecurityInfoW(native, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, readOnlyAcl, nullptr), DWORD(ERROR_SUCCESS));
        QFile denied(sdk_ + "/forbidden.txt"); QVERIFY(!denied.open(QIODevice::WriteOnly));
        const auto previousPath = qgetenv("PATH");
        qputenv("PATH", "C:/Windows/System32");
        const auto restorePath = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });
        const auto built = run(msbuild_, {project, "/t:Build", "/m:1", "/nr:false", "/v:minimal", "/p:Configuration=" + configuration_ + ";Platform=x64"}, root_, variant + "-build.log");
        QVERIFY2(built.status == ProcessResult::Status::Success, built.output.constData());
        const auto fresh = check(project); QCOMPARE(fresh.value("status").toString(), "compiled");
        const auto valid = check(project, true); QVERIFY2(valid.value("status") == "target-validated", QJsonDocument(valid).toJson().constData());
        QCOMPARE(Native::inventory(sdk_), beforeSdk);
        const QString source = c.destination + "/Extension.cpp"; const auto original = bytes(source); auto changed = original;
        const auto pos = changed.indexOf("extension"); QVERIFY(pos >= 0); changed[pos] = 'E'; put(source, changed);
        QCOMPARE(check(project).value("status").toString(), "stale"); put(source, original);
        const QString current = c.destination + "/.artest/native/" + configuration_ + "/current";
        const QString dll = QDir(current + "/package").entryList({"*.dll"}, QDir::Files).value(0); QVERIFY(!dll.isEmpty());
        const QString binary = current + "/package/" + dll; const auto binaryBytes = bytes(binary); auto brokenBinary = binaryBytes; brokenBinary[100] = char(brokenBinary[100] ^ 1);
        put(binary, brokenBinary); QCOMPARE(check(project).value("status").toString(), "failed"); put(binary, binaryBytes);
        QVERIFY(QFile::remove(binary)); QCOMPARE(check(project).value("status").toString(), "failed"); put(binary, binaryBytes);
        auto alteredRequest = Native::readObject(current + "/inputs.json").value("request").toObject(); alteredRequest["toolchain"] = root_ + "/Different toolchain";
        Native::writeObject(root_ + "/incompatible-request.json", alteredRequest);
        const auto mismatch = run(sdk_ + "/ARTestDevNative.exe", {"inspect", root_ + "/incompatible-request.json"}, root_, variant + "-toolchain.log");
        QCOMPARE(QJsonDocument::fromJson(mismatch.output).object().value("status").toString(), "stale");
        const QString brokenSdk = root_ + "/Broken SDK " + variant; copy(sdk_, brokenSdk);
        const QString header = brokenSdk + "/native-sdk/include/ARTestExtensionAbi.h"; auto badHeader = bytes(header); badHeader[0] = char(badHeader[0] ^ 1); put(header, badHeader);
        const auto sdkMismatch = run(sdk_ + "/ARTestDevNative.exe", {"check-project", project, configuration_, brokenSdk + "/native-sdk"}, root_, variant + "-sdk.log");
        QCOMPARE(QJsonDocument::fromJson(sdkMismatch.output).object().value("status").toString(), "failed");
        const QString manifestPath = current + "/package/artest-extension.json"; const auto manifestBytes = bytes(manifestPath);
        put(manifestPath, manifestBytes + " "); QCOMPARE(check(project).value("status").toString(), "failed"); put(manifestPath, manifestBytes);
        put(current + "/unknown.txt", "foreign"); QCOMPARE(check(project).value("status").toString(), "failed"); QVERIFY(QFile::remove(current + "/unknown.txt"));
        // Fault fixture: an internally consistent inventory with a false descriptor must reach Engine and fail there.
        const auto ownerBytes = bytes(current + "/ownership.json"); auto manifest = Native::readObject(manifestPath);
        auto components = manifest.value("components").toArray(); QVERIFY(!components.isEmpty()); auto component = components[0].toObject(); component["typeId"] = "invalid.descriptor.fixture"; components[0] = component; manifest["components"] = components;
        Native::writeObject(manifestPath, manifest); auto owner = Native::readObject(current + "/ownership.json"); auto files = Native::inventory(current); files.remove("ownership.json"); owner["files"] = files; Native::writeObject(current + "/ownership.json", owner);
        const auto invalidDescriptor = check(project, true);
        QCOMPARE(invalidDescriptor.value("status").toString(), "failed");
        QVERIFY2(invalidDescriptor.value("diagnostic").toString().contains("descriptors.log"), QJsonDocument(invalidDescriptor).toJson().constData());
        put(manifestPath, manifestBytes); put(current + "/ownership.json", ownerBytes);
        QCOMPARE(check(project).value("status").toString(), "compiled");
        if (variant == "driver-command") {
            const auto retained = Native::inventory(current);
            put(source, original + "\n#error expected DEV014 compiler failure\n");
            const auto failed = run(msbuild_, {project, "/t:Build", "/m:1", "/nr:false", "/v:minimal", "/p:Configuration=" + configuration_ + ";Platform=x64"}, root_, "expected-compiler-failure.log");
            QVERIFY(failed.status != ProcessResult::Status::Success);
            QCOMPARE(Native::inventory(current), retained);
            QVERIFY(!QFileInfo::exists(c.destination + "/.artest/native/" + configuration_ + "/transaction.json"));
            put(source, original);
            const QString fake = root_ + "/Incompatible CLI"; QVERIFY(QDir().mkdir(fake));
            QVERIFY(QFile::copy(sdk_ + "/ARTestDevNative.exe", fake + "/ARTestCLI.exe"));
            QVERIFY(QFile::copy(QFileInfo(cli_).absolutePath() + "/ARTestEngine.dll", fake + "/ARTestEngine.dll"));
            for (const QString &f : QDir(sdk_).entryList({"Qt6*.dll"}, QDir::Files)) QVERIFY(QFile::copy(sdk_ + '/' + f, fake + '/' + f));
            const QString selected = cli_; cli_ = fake + "/ARTestCLI.exe";
            QVERIFY(check(project, true).value("status") != "target-validated");
            cli_ = root_ + "/Missing CLI/ARTestCLI.exe";
            QVERIFY(check(project, true).value("status") != "target-validated"); cli_ = selected;
            QCOMPARE(Native::inventory(current), retained);
        }
    }


    void compilerTail_data() {
        QTest::addColumn<QString>("mode");
        for (const char *mode : {"default-strict", "permitted", "foreign-path", "unconfirmed"}) QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void compilerTail() {
        QFETCH(QString, mode);
        const QString dir = root_ + "/tail-" + mode; QVERIFY(QDir().mkdir(dir)); QVERIFY(QDir().mkdir(dir + "/foreign"));
        const QString probe = QCoreApplication::applicationDirPath() + "/ARTestDevNativeProcessProbe.exe";
        QVERIFY(QFile::copy(probe, dir + "/vctip.exe")); QVERIFY(QFile::copy(probe, dir + "/foreign/vctip.exe"));
        const QString child = mode == "foreign-path" ? dir + "/foreign/vctip.exe" : dir + "/vctip.exe", marker = dir + "/writer.txt";
        TreeProcess process; QEventLoop loop; ProcessResult result; bool blocked = false;
        if (mode != "default-strict") process.permitCompilerTelemetryTail(dir + "/vctip.exe");
        if (mode == "unconfirmed") process.suppressTerminationForTest();
        connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &r) {
            result = r;
            if (r.status == ProcessResult::Status::TerminationUnconfirmed) blocked = process.busy() && !process.start(probe, {}, dir);
            else loop.quit();
        });
        connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
        QVERIFY(process.start(probe, {"parent", child, marker}, dir, 10000)); loop.exec();
        QVERIFY(!process.busy());
        if (mode == "permitted") QCOMPARE(result.status, ProcessResult::Status::Success);
        else if (mode == "unconfirmed") { QCOMPARE(result.status, ProcessResult::Status::TerminationUnconfirmed); QVERIFY(blocked); }
        else QCOMPARE(result.status, ProcessResult::Status::Failed);
        QCOMPARE(QFileInfo::exists(marker), mode == "unconfirmed");
        put(dir + "/result.log", result.output + result.detail.toUtf8());
    }

    void interrupted_data() {
        QTest::addColumn<QString>("phase");
        for (const char *p : {"ready", "backup-renamed", "current-renamed"}) QTest::newRow(p) << QString::fromLatin1(p);
    }
    void interrupted() {
        QFETCH(QString, phase);
        const QString state = root_ + "/killed-" + QUuid::createUuid().toString(QUuid::Id128), project = root_ + "/fixture.vcxproj";
        const QString revision = QUuid::createUuid().toString(QUuid::Id128), assembled = state + "/work-" + revision + "/assembled";
        const auto make = [&](const QString &path, const QString &id) {
            QDir().mkpath(path); put(path + "/same", "identical output bytes");
            Native::writeObject(path + "/ownership.json", {{"format", "ARTestDev.NativeOutput.1"}, {"project", project}, {"revision", id}, {"files", Native::inventory(path)}});
        };
        make(state + "/current", "old"); make(assembled, revision);
        QCOMPARE(Native::readObject(state + "/current/ownership.json").value("files"), Native::readObject(assembled + "/ownership.json").value("files"));
        TreeProcess process; QEventLoop loop; QTimer poll; bool reached = false, excluded = false; ProcessResult result;
        const QString marker = root_ + "/publication-pause-" + phase;
        qputenv("ARTESTDEV_FAULT_PHASE", phase.toUtf8()); qputenv("ARTESTDEV_FAULT_MARKER", marker.toUtf8());
        const auto reset = qScopeGuard([] { qunsetenv("ARTESTDEV_FAULT_PHASE"); qunsetenv("ARTESTDEV_FAULT_MARKER"); });
        connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &r) { result = r; if (!process.busy()) loop.quit(); });
        connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
        connect(&poll, &QTimer::timeout, &loop, [&] {
            if (!QFileInfo::exists(marker) || reached) return;
            const auto journal = Native::readObject(state + "/transaction.json");
            const bool current = QFileInfo::exists(state + "/current"), backup = QFileInfo::exists(state + "/backup");
            reached = (phase == "ready" && journal.value("phase") == "ready" && current && !backup) ||
                (phase == "backup-renamed" && journal.value("phase") == "ready" && !current && backup) ||
                (phase == "current-renamed" && journal.value("phase") == "backed-up" && current && backup);
            if (!reached) return;
            try { Native::publishFixture(state, project, revision); } catch (const std::exception &) { excluded = true; }
            process.cancel();
        });
        QVERIFY(process.start(QCoreApplication::applicationDirPath() + "/ARTestDevPublicationProbe.exe", {state, project, revision}, root_, 20000));
        poll.start(20); loop.exec(); poll.stop();
        put(root_ + "/publisher-" + phase + ".log", result.output);
        QVERIFY2(reached, result.output.constData()); QVERIFY(excluded); QVERIFY(result.status == ProcessResult::Status::Cancelled); QVERIFY(!process.busy());
        put(state + "/../killed-" + phase + ".log", result.output + "\nTermination confirmed; concurrent writer excluded.\n");
        Native::recover(state, project);
        QCOMPARE(Native::readObject(state + "/current/ownership.json").value("revision").toString(), phase == "current-renamed" ? revision : "old");
        QVERIFY(!QFileInfo::exists(state + "/transaction.json"));
    }
    void recoveryGuards() {
        const QString state = root_ + "/guards", project = root_ + "/fixture.vcxproj";
        QVERIFY(QDir().mkdir(state)); QVERIFY(QDir().mkdir(state + "/current")); put(state + "/current/same", "payload");
        Native::writeObject(state + "/current/ownership.json", {{"format", "ARTestDev.NativeOutput.1"}, {"project", project}, {"revision", "old"}, {"files", Native::inventory(state + "/current")}});
        Native::writeObject(state + "/transaction.json", {{"format", "ARTestDev.NativeTransaction.1"}, {"project", project}, {"state", state}, {"revision", "new"}, {"previous", true}, {"phase", "building"}});
        const auto journalBytes = bytes(state + "/transaction.json");
        const auto expectRejected = [&] {
            bool rejected = false; try { Native::recover(state, project); } catch (const std::exception &) { rejected = true; }
            return rejected;
        };
        auto duplicate = journalBytes; duplicate.chop(1); duplicate += ",\"phase\":\"ready\"}";
        put(state + "/transaction.json", duplicate); QVERIFY(expectRejected()); QCOMPARE(bytes(state + "/transaction.json"), duplicate);
        put(state + "/transaction.json", journalBytes);
        put(state + "/unknown.txt", "foreign"); const auto before = Native::inventory(state);
        QVERIFY(expectRejected()); QCOMPARE(Native::inventory(state), before); QVERIFY(QFile::remove(state + "/unknown.txt"));
        QVERIFY(makeJunction(state + "/current/link", sdk_));
        QVERIFY(expectRejected()); QVERIFY(QFileInfo::exists(sdk_ + "/ARTestDev.exe"));
        QVERIFY(RemoveDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(state + "/current/link").utf16())));
        auto owner = Native::readObject(state + "/current/ownership.json"); owner["project"] = "different owner"; Native::writeObject(state + "/current/ownership.json", owner);
        QVERIFY(expectRejected());
    }
    void asyncInspection() {
        NativeService service; service.setExecutable(sdk_ + "/ARTestDevNative.exe");
        const QString missing = root_ + "/Missing"; QVERIFY(QDir().mkdir(missing));
        QEventLoop loop; QTimer clock; int ticks = 0; QJsonObject report;
        connect(&clock, &QTimer::timeout, &loop, [&] { ++ticks; });
        connect(&service, &NativeService::completed, &loop, [&](const QJsonObject &r, const ProcessResult &) { report = r; loop.quit(); });
        QVERIFY(service.start(missing + "/Missing.vcxproj", configuration_));
        QVERIFY(!service.start(missing + "/Missing.vcxproj", configuration_));
        clock.start(1); loop.exec(); clock.stop();
        QVERIFY(ticks > 0); QVERIFY(!service.busy()); QCOMPARE(report.value("status").toString(), "missing");
    }

    void cleanupInterruptions_data() {
        QTest::addColumn<QString>("phase");
        for (const char *p : {"before-promotion", "backup-retired", "rollback-restored", "candidate-retired", "before-journal-retirement", "rollback-journal", "work-sealed"}) QTest::newRow(p) << QString::fromLatin1(p);
    }
    void cleanupInterruptions() {
        QFETCH(QString, phase);
        const QString state = root_ + "/cleanup-" + phase, project = root_ + "/fixture.vcxproj";
        const QString revision = QUuid::createUuid().toString(QUuid::Id128), work = state + "/work-" + revision;
        const bool rollback = phase == "rollback-restored" || phase == "candidate-retired" || phase == "rollback-journal";
        const bool first = phase == "before-promotion";
        const auto make = [&](const QString &path, const QString &id) {
            QDir().mkpath(path + "/empty"); put(path + "/same", "identical payload");
            QJsonObject owner{{"format", "ARTestDev.NativeOutput.1"}, {"project", project}, {"revision", id}, {"files", Native::inventory(path)}};
            Native::writeObject(path + "/ownership.json", owner); return owner;
        };
        QJsonObject prior;
        if (!first) prior = make(state + (rollback ? "/backup" : "/current"), "old");
        const auto next = make(rollback ? state + "/candidate" : work + "/assembled", revision);
        if (!first) QCOMPARE(prior.value("files"), next.value("files"));
        if (rollback) Native::writeObject(state + "/transaction.json", {{"format", "ARTestDev.NativeTransaction.1"}, {"project", project}, {"state", state}, {"revision", revision}, {"previous", true}, {"phase", "ready"}, {"candidateOwner", next}, {"previousOwner", prior}});
        const QString marker = root_ + "/fault-" + phase;
        qputenv("ARTESTDEV_FAULT_PHASE", phase == "rollback-journal" ? QByteArray("before-journal-retirement") : phase.toUtf8()); qputenv("ARTESTDEV_FAULT_MARKER", marker.toUtf8());
        const auto reset = qScopeGuard([] { qunsetenv("ARTESTDEV_FAULT_PHASE"); qunsetenv("ARTESTDEV_FAULT_MARKER"); });
        TreeProcess process; QEventLoop loop; QTimer poll; ProcessResult result; bool reached = false;
        connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &r) { result = r; if (!process.busy()) loop.quit(); });
        connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
        connect(&poll, &QTimer::timeout, &loop, [&] { if (QFileInfo::exists(marker)) { reached = true; process.cancel(); } });
        QVERIFY(process.start(QCoreApplication::applicationDirPath() + "/ARTestDevPublicationProbe.exe", {state, project, rollback ? "recover" : revision}, root_, 20000));
        poll.start(20); loop.exec(); poll.stop();
        QVERIFY(reached); QCOMPARE(result.status, ProcessResult::Status::Cancelled); QVERIFY(!process.busy());
        qunsetenv("ARTESTDEV_FAULT_PHASE");
        put(root_ + "/interrupted-" + phase + ".json", QJsonDocument(Native::inventory(state)).toJson());
        const QString output = QFileInfo::exists(work + "/retired-backup") ? work + "/retired-backup" :
            QFileInfo::exists(work + "/retired-candidate") ? work + "/retired-candidate" :
            QFileInfo::exists(state + "/candidate") ? state + "/candidate" : state + "/current";
        const auto rejectedUnchanged = [&] {
            const auto before = Native::inventory(state); bool rejected = false;
            try { Native::recover(state, project); } catch (const std::exception &) { rejected = true; }
            return rejected && Native::inventory(state) == before;
        };
        const auto ownerBytes = bytes(output + "/ownership.json"); auto changed = Native::readObject(output + "/ownership.json");
        changed["revision"] = "foreign-but-identical-payload"; Native::writeObject(output + "/ownership.json", changed);
        QVERIFY(rejectedUnchanged()); put(output + "/ownership.json", ownerBytes);
        put(output + "/same", "corrupt payload"); QVERIFY(rejectedUnchanged()); put(output + "/same", "identical payload");
        put(output + "/foreign", "unknown"); QVERIFY(rejectedUnchanged()); QVERIFY(QFile::remove(output + "/foreign"));
        QVERIFY(makeJunction(output + "/link", sdk_));
        bool reparseRejected = false; try { Native::recover(state, project); } catch (const std::exception &) { reparseRejected = true; }
        QVERIFY(reparseRejected); QVERIFY(RemoveDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(output + "/link").utf16())));
        Native::recover(state, project);
        QVERIFY(!QFileInfo::exists(state + "/transaction.json"));
        if (first) QVERIFY(!QFileInfo::exists(state + "/current"));
        else QCOMPARE(Native::readObject(state + "/current/ownership.json"), rollback ? prior : next);
        const auto after = Native::inventory(state); Native::recover(state, project); QCOMPARE(Native::inventory(state), after);
        // A subsequent writer can publish after every recovered window.
        const QString another = QUuid::createUuid().toString(QUuid::Id128);
        make(state + "/work-" + another + "/assembled", another);
        Native::publishFixture(state, project, another);
        QCOMPARE(Native::readObject(state + "/current/ownership.json").value("revision").toString(), another);
    }
    void recovery_data() {
        QTest::addColumn<QString>("phase"); QTest::addColumn<bool>("current"); QTest::addColumn<bool>("candidate"); QTest::addColumn<bool>("backup"); QTest::addColumn<bool>("accepted");
        QTest::newRow("before-rename") << "ready" << true << true << false << true;
        QTest::newRow("backup-before-phase") << "ready" << false << true << true << true;
        QTest::newRow("after-backup") << "backed-up" << false << true << true << true;
        QTest::newRow("promotion-before-phase") << "backed-up" << true << false << true << true;
        QTest::newRow("promoted") << "promoted" << true << false << true << true;
        QTest::newRow("ambiguous") << "promoted" << true << true << true << false;
        QTest::newRow("interrupted-build") << "building" << true << false << false << false;
    }
    void recovery() {
        QFETCH(QString, phase); QFETCH(bool, current); QFETCH(bool, candidate); QFETCH(bool, backup); QFETCH(bool, accepted);
        const QString state = root_ + "/recovery-" + QUuid::createUuid().toString(QUuid::Id128), project = root_ + "/fixture.vcxproj"; QVERIFY(QDir().mkdir(state));
        const QString revision = QUuid::createUuid().toString(QUuid::Id128);
        const auto make = [&](const QString &name, const QString &id) {
            const auto path = state + '/' + name; QDir().mkdir(path); put(path + "/same", "identical inventory");
            QJsonObject owner{{"format", "ARTestDev.NativeOutput.1"}, {"project", project}, {"revision", id}, {"files", Native::inventory(path)}};
            Native::writeObject(path + "/ownership.json", owner); return owner;
        };
        QJsonObject prior, next;
        if (current) { const auto o = make("current", !candidate ? revision : "old"); if (!candidate) next = o; else prior = o; }
        if (candidate) next = make("candidate", revision);
        if (backup) prior = make("backup", "old");
        Native::writeObject(state + "/transaction.json", {{"format", "ARTestDev.NativeTransaction.1"}, {"project", project}, {"state", state}, {"revision", revision}, {"previous", true}, {"phase", phase}, {"candidateOwner", next}, {"previousOwner", prior}});
        const auto before = Native::inventory(state); bool success = true;
        try { Native::recover(state, project); } catch (const std::exception &) { success = false; }
        QCOMPARE(success, accepted);
        if (accepted) { QVERIFY(QFileInfo::exists(state + "/current/same")); QVERIFY(!QFileInfo::exists(state + "/transaction.json")); const auto after = Native::inventory(state); Native::recover(state, project); QCOMPARE(Native::inventory(state), after); }
        else QCOMPARE(Native::inventory(state), before);
    }
};
QTEST_GUILESS_MAIN(NativeTests)
#include "NativeTests.moc"

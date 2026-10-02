#include "TestPlanService.h"
#include "RegistrationStore.h"
#include "PreparationService.h"
#include "NativeService.h"
#include "SdkLocation.h"
#include "TreeProcess.h"
#include <QtConcurrentRun>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QUuid>
#include <QSet>
#include <QMap>
#include <functional>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ARTestDev {
namespace {
using namespace Registration;
using Notice = std::function<void(const QString &)>;
QString extendedPath(const QString &path) { return QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(absolute(path)); }
// Deny ordinary writes/renames to pinned files across both CLI invocations.
// Catalog topology is checked again before execution; this is not a sandbox.
struct Pins {
    std::vector<HANDLE> handles;
    QSet<QString> paths;
    ~Pins() { for (auto handle : handles) CloseHandle(handle); }
    void file(const QString &path) {
        const auto key = absolute(path).toCaseFolded(); if (paths.contains(key)) return;
        ordinary(path); require(handles.size() < 32768, "Demasiados inputs; no se ejecuta");
        for (QString p = absolute(path);;) {
            const auto attrs = GetFileAttributesW(reinterpret_cast<LPCWSTR>(extendedPath(p).utf16()));
            require(attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "Input ausente/reparse; conserve e inspeccione: " + p);
            const auto parent = QFileInfo(p).absolutePath(); if (parent == p) break; p = parent;
        }
        const auto h = CreateFileW(reinterpret_cast<LPCWSTR>(extendedPath(path).utf16()), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto error = GetLastError();
        require(h != INVALID_HANDLE_VALUE, "Input ocupado o inaccesible (Windows " + QString::number(error) + "); cierre el escritor y valide otra vez: " + path);
        handles.push_back(h); paths.insert(key);
    }
    QJsonObject tree(const QString &root) {
        const auto before = inventory(root);
        for (auto it = before.begin(); it != before.end(); ++it) file(root + '/' + it.key());
        require(inventory(root) == before, "Inputs cambiaron durante captura; valide otra vez"); return before;
    }
};
struct Operation {
    std::shared_ptr<std::atomic_int> decision;
    Notice notice;
    TestPlanService::ProcessFactory factory;
    void checkpoint() { require(decision->load() != -1, "Cancelado. No se inicia otra ejecucion."); }
    ProcessResult cli(const QString &program, const QStringList &args, int bound, bool *attempted = nullptr) {
        checkpoint(); auto owned = factory ? factory() : std::make_unique<TreeProcess>(); auto &process = *owned;
        QEventLoop loop; ProcessResult result;
        QObject::connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &p) {
            result = p;
            if (p.status == ProcessResult::Status::TerminationUnconfirmed)
                notice("TERMINACION NO CONFIRMADA. Operacion y destino bloqueados hasta observar salida. No se confirma limpieza ni efectos externos.");
            if (!process.busy()) loop.quit();
        });
        QObject::connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
        QTimer timer; QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (decision->load() == -1) process.cancel(); }); timer.start(50);
        require(process.start(program, args, QFileInfo(program).absolutePath(), bound, 1024 * 1024), "No se pudo iniciar CLI; compruebe instalacion y permisos");
        if (attempted) *attempted = true;
        loop.exec(); return result;
    }
    QString localPackage(const TestPlanRequest &r, QString &receipt, QString &revision) {
        checkpoint(); QEventLoop loop; QTimer timer;
        if (r.project.language == "python") {
            PreparationService service(r.stagedExecutable); PreparationResult result; bool received = false;
            QObject::connect(&service, &PreparationService::completed, &loop, [&](const PreparationResult &p) {
                result = p; received = true;
                if (p.status == PreparationResult::Status::TerminationUnconfirmed) notice(p.diagnostic + "\nPreparacion aun activa; espere.");
                if (!service.busy()) loop.quit();
            });
            QObject::connect(&service, &PreparationService::settled, &loop, [&] { if (received) loop.quit(); });
            QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (decision->load() == -1) service.cancel(); }); timer.start(50);
            require(service.start(r.project.root, r.python), "No se puede preparar Python; seleccione un interprete compatible"); loop.exec();
            require(received && (result.status == PreparationResult::Status::Prepared || result.status == PreparationResult::Status::Reused), result.diagnostic + '\n' + QString::fromUtf8(result.output));
            checkpoint(); receipt = result.receipt; revision = result.identity; return result.package;
        }
        NativeService service; service.setExecutable(QFileInfo(r.stagedExecutable).absolutePath() + "/ARTestDevNative.exe");
        QJsonObject report; ProcessResult result;
        QObject::connect(&service, &NativeService::completed, &loop, [&](const QJsonObject &j, const ProcessResult &p) {
            report = j; result = p;
            if (p.status == ProcessResult::Status::TerminationUnconfirmed) notice(p.detail);
            if (!service.busy()) loop.quit();
        });
        QObject::connect(&service, &NativeService::settled, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (decision->load() == -1) service.cancel(); }); timer.start(50);
        // check-project verifies IDE provenance without loading native components.
        require(service.start(r.project.projectFile, r.configuration), "Seleccione configuracion Debug/Release"); loop.exec();
        require(result.status == ProcessResult::Status::Success && report.value("status") == "compiled", "Build requerido: compile en Visual Studio la configuracion seleccionada y vuelva a validar.\n" + report.value("diagnostic").toString() + result.detail);
        checkpoint(); const auto package = r.project.root + "/.artest/native/" + r.configuration + "/current/package";
        revision = hash(QJsonDocument(inventory(package)).toJson(QJsonDocument::Compact)); return package;
    }
};
QJsonObject finalReport(const QByteArray &output) {
    // extension-run appends one result object after lifecycle text. Never infer a verdict from exit 0.
    int candidates = 0;
    for (qsizetype pos = output.lastIndexOf('\n'); pos >= 0; pos = output.lastIndexOf('\n', pos - 1)) {
        if (pos + 1 < output.size() && output.at(pos + 1) == '{') {
            if (++candidates > 64) return {};
            const auto doc = QJsonDocument::fromJson(output.mid(pos + 1));
            if (doc.isObject() && doc.object().value("schema") == "artest.schema.run-result.v2") return doc.object();
        }
        if (pos == 0) break;
    }
    const auto doc = QJsonDocument::fromJson(output); return doc.isObject() && doc.object().value("schema") == "artest.schema.run-result.v2" ? doc.object() : QJsonObject{};
}
TestPlanResult run(TestPlanRequest r, Operation &op, const std::function<void(const TestPlanResult &)> &ready) {
    TestPlanResult result;
    try {
        op.checkpoint(); checkProfile(r.target);
        require(r.project.valid && QDir::isAbsolutePath(r.project.root) && !r.project.extensionId.isEmpty(), "Abra un proyecto valido");
        require(r.executionTimeoutMs > 0 && r.executionTimeoutMs <= 1800000, "Limite de ejecucion invalido");
        const auto config = absolute(r.target.value("configuration").toString()), catalog = absolute(r.target.value("catalog").toString());
        Lock selection(r.registryRoot + "/.artest-installations.lock");
        Lock target(QFileInfo(config).absolutePath() + "/." + QFileInfo(config).fileName() + ".artest-register.lock");
        require(!QFileInfo::exists(config + "/.artest-register-transaction.json") && !QFileInfo::exists(config + "/.artest-register-transaction.json.recovery"), "Registro pendiente de recuperacion; vuelva a Integrate antes de ejecutar");
        const auto state = load(config + "/registrations.json");
        require(state.value("schema") == "artest.schema.sdk-registration-state.v1" && state.value("schemaVersion") == 1, "Estado de registro incompatible; inspeccione destino");
        const auto registered = state.value("registrations").toObject().value(r.project.extensionId).toObject();
        require(r.sources || !registered.isEmpty(), "No hay revision integrada de este proyecto. Use Integrate o seleccione Fuentes locales explicitamente.");
        const auto work = r.project.root + "/.artest/test-runs/" + QUuid::createUuid().toString(QUuid::Id128);
        ordinary(work); require(!QFileInfo::exists(work), "Carpeta de evidencia ocupada"); result.evidence = work;
        const auto plan = work + "/test-plan.json", mappingPath = work + "/python-environments.json";
        write(plan, read(r.plan));
        auto mapping = load(config + "/python-environments.json");
        QString executionCatalog = catalog;
        Pins pins; const auto installed = pins.tree(catalog);
        pins.file(config + "/registrations.json"); pins.file(config + "/python-environments.json");
        pins.file(r.registryRoot + "/installations.json");
        if (r.sources) {
            const auto sdk = inspectStagingExecutable(r.stagedExecutable); require(sdk.valid, sdk.diagnostics.join('\n'));
            QString receipt; const auto package = op.localPackage(r, receipt, result.revision);
            const auto localFiles = pins.tree(package);
            if (r.project.language == "cpp") require(result.revision == hash(QJsonDocument(localFiles).toJson(QJsonDocument::Compact)), "Salida C++ cambio despues de comprobar Build; valide otra vez");
            require(load(package + "/artest-extension.json").value("extensionId") == r.project.extensionId, "Las fuentes tienen otra identidad; abra el proyecto correcto");
            executionCatalog = work + "/catalog"; require(QDir().mkpath(executionCatalog), "No se puede crear catalogo local");
            QSet<QString> ids;
            for (const auto &dir : QDir(catalog).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
                const auto manifest = catalog + '/' + dir + "/artest-extension.json";
                const auto id = QFileInfo::exists(manifest) ? load(manifest).value("extensionId").toString() : QString();
                require(id.isEmpty() || !ids.contains(id), "Catalogo instalado con IDs duplicados"); ids.insert(id);
                if (id != r.project.extensionId) copy(catalog + '/' + dir, executionCatalog + '/' + dir);
            }
            copy(package, executionCatalog + "/project-local-" + hash(r.project.extensionId.toUtf8()).left(16));
            if (receipt.isEmpty()) mapping.remove(r.project.extensionId); else mapping[r.project.extensionId] = receipt;
        } else {
            const auto name = registered.value("packageName").toString();
            require(!name.isEmpty() && name != "." && name != ".." && QFileInfo(name).fileName() == name && !name.contains('\\'), "Nombre de paquete registrado invalido");
            require(inventory(catalog + '/' + name) == registered.value("inventory").toObject(), "Revision registrada alterada; inspeccione o integre de nuevo");
            require(load(catalog + '/' + name + "/artest-extension.json").value("extensionId") == r.project.extensionId, "Identidad registrada incorrecta");
            result.revision = registered.value("revisionId").toString(); require(!result.revision.isEmpty(), "Revision registrada ausente");
        }
        save(mappingPath, mapping); pins.file(plan); pins.file(mappingPath);
        const auto executionInventory = pins.tree(executionCatalog);
        QMap<QString, QJsonObject> environments;
        for (auto it = mapping.begin(); it != mapping.end(); ++it) {
            require(it.value().isString() && QDir::isAbsolutePath(it.value().toString()), "Asociacion Python invalida");
            const auto root = QFileInfo(it.value().toString()).absolutePath();
            if (!environments.contains(root)) environments[root] = pins.tree(root);
            const auto receipt = load(it.value().toString());
            pins.file(receipt.value("interpreter").toString());
            for (const auto &v : receipt.value("runtimeFiles").toArray()) pins.file(v.toObject().value("path").toString());
        }
        const auto cli = absolute(r.target.value("cli").toString());
        for (const auto &name : QDir(QFileInfo(cli).absolutePath()).entryList({"*.exe", "*.dll"}, QDir::Files)) pins.file(QFileInfo(cli).absolutePath() + '/' + name);
        const auto planHash = digest(plan);
        save(work + "/inputs.json", {{"mode", r.sources ? "sources" : "registered"}, {"target", r.target}, {"revision", result.revision}, {"selectedPlan", r.plan}, {"planSha256", planHash}, {"catalog", executionCatalog}, {"inventory", executionInventory}, {"mapping", mapping}, {"cliSha256", digest(cli)}, {"engineSha256", digest(r.target.value("engine").toString())}});
        op.notice("Destino: " + cli + "\nModo: " + (r.sources ? "Fuentes locales" : "Revision integrada") + "\nRevision: " + result.revision + "\nTest plan SHA-256: " + planHash + "\nValidando offline…");
        const auto discovered = op.cli(cli, {"extensions", "validate", executionCatalog}, 120000);
        write(work + "/catalog.txt", discovered.output);
        require(discovered.status == ProcessResult::Status::Success, "Catalogo/CLI no compatible; no se ejecuta.\n" + discovered.detail + '\n' + QString::fromUtf8(discovered.output));
        const auto catalogReport = object(discovered.output);
        require(catalogReport.value("schema") == "artest.schema.extension-catalog.v2" && catalogReport.value("valid").toBool() && catalogReport.value("abi").toObject() == QJsonObject{{"major", 0}, {"minor", 2}}, "CLI/Engine incompatibles; seleccione una instalacion compatible mediante Integrate");
        result.process = op.cli(cli, {"compile", plan, "--extensions", executionCatalog, "--python-environments", mappingPath}, 120000);
        write(work + "/compile.txt", result.process.output);
        require(result.process.status == ProcessResult::Status::Success, "Validacion offline rechazada; no se ejecuto. Corrija el Test plan/diagnostico CLI y valide de nuevo.\n" + result.process.detail + '\n' + QString::fromUtf8(result.process.output));
        op.checkpoint(); result.validated = true; ready(result);
        QEventLoop waiting; QTimer poll; QElapsedTimer elapsed; elapsed.start();
        QObject::connect(&poll, &QTimer::timeout, &waiting, [&] { if (op.decision->load() != 0 || elapsed.elapsed() >= 600000) waiting.quit(); }); poll.start(50); waiting.exec();
        op.checkpoint(); require(op.decision->load() == 1, "Validacion expirada tras 10 minutos; vuelva a validar. No se ejecuto.");
        require(inventory(catalog) == installed && inventory(executionCatalog) == executionInventory && digest(plan) == planHash, "Inputs cambiaron tras validacion; no se ejecuta. Valide otra vez.");
        for (auto it = environments.begin(); it != environments.end(); ++it) require(inventory(it.key()) == it.value(), "Entorno Python cambio; valide otra vez");
        op.checkpoint(); op.notice("Ejecutando explicitamente la revision validada…");
        result.process = op.cli(cli, {"extension-run", plan, executionCatalog, "--python-environments", mappingPath}, r.executionTimeoutMs, &result.executed);
        write(work + "/execution.txt", result.process.output); result.report = finalReport(result.process.output);
        const QStringList statuses{"Success", "Failed", "StartFailed", "Cancelled", "Timeout", "OutputLimit", "TerminationUnconfirmed"};
        result.diagnostic = "Supervisor: " + statuses.at(int(result.process.status)) + "\nCLI exit: " + QString::number(result.process.exitCode) + '\n' + result.process.detail;
        if (result.process.status != ProcessResult::Status::Success && result.process.status != ProcessResult::Status::Failed) {
            result.diagnostic += "\nInterrupcion del supervisor (cancelacion, timeout o limite de salida). Efectos y limpieza NO confirmados. No se reintenta; inspeccione antes de otra ejecucion.";
        }
        if (result.report.isEmpty()) result.diagnostic += "\nSin resultado final del Engine; no se atribuye PASS ni limpieza confirmada.\n" + QString::fromUtf8(result.process.output);
        else result.diagnostic += '\n' + QString::fromUtf8(QJsonDocument(result.report).toJson(QJsonDocument::Indented));
    } catch (const std::exception &e) { result.diagnostic = QString::fromUtf8(e.what()); }
    if (!result.evidence.isEmpty()) {
        try { save(result.evidence + "/result.json", {{"validated", result.validated}, {"executed", result.executed}, {"revision", result.revision}, {"processStatus", int(result.process.status)}, {"exitCode", result.process.exitCode}, {"report", result.report}, {"diagnostic", result.diagnostic}}); }
        catch (const std::exception &e) { result.diagnostic += "\nNo se pudo guardar evidencia: " + QString::fromUtf8(e.what()); }
    }
    return result;
}
}
TestPlanService::TestPlanService(QObject *parent, ProcessFactory factory) : QObject(parent), factory_(std::move(factory)) {
    connect(&worker_, &QFutureWatcher<TestPlanResult>::finished, this, [this] { running_ = ready_ = false; emit completed(worker_.result()); });
}
TestPlanService::~TestPlanService() { cancel(); worker_.waitForFinished(); }
bool TestPlanService::start(const TestPlanRequest &request) {
    if (running_) return false;
    running_ = true; ready_ = false; decision_ = std::make_shared<std::atomic_int>(0);
    auto r = request; if (r.registryRoot.isEmpty()) r.registryRoot = configurationRoot();
    if (r.stagedExecutable.isEmpty()) r.stagedExecutable = QCoreApplication::applicationFilePath();
    auto decision = decision_; auto factory = factory_;
    worker_.setFuture(QtConcurrent::run([this, r, decision, factory] {
        Operation op{decision, [this](const QString &s) { QMetaObject::invokeMethod(this, [this, s] { emit progress(s); }, Qt::QueuedConnection); }, factory};
        return run(r, op, [this, decision](const TestPlanResult &v) { QMetaObject::invokeMethod(this, [this, decision, v] { if (decision->load() == 0) { ready_ = true; emit validated(v); } }, Qt::QueuedConnection); });
    })); return true;
}
void TestPlanService::execute() { if (running_ && ready_) { ready_ = false; int expected = 0; decision_->compare_exchange_strong(expected, 1); } }
void TestPlanService::cancel() { ready_ = false; if (decision_) decision_->store(-1); }
}

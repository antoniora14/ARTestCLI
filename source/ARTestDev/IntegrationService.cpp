#include "IntegrationService.h"
#include "RegistrationStore.h"
#include "PreparationService.h"
#include "NativeService.h"
#include "SdkLocation.h"
#include "TreeProcess.h"
#include "Presentation.h"
#include <QtConcurrentRun>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <functional>

namespace ARTestDev {
namespace {
using namespace Registration;
using Notice = std::function<void(const QString &)>;
struct Runner {
    std::shared_ptr<std::atomic_bool> cancelled;
    Notice notice;
    void checkpoint() const { require(!cancelled->load(), "Cancelado; no se confirma una nueva integracion"); }
    QByteArray run(const QString &cli, const QStringList &args) {
        checkpoint(); TreeProcess process; QEventLoop loop; ProcessResult result; bool received = false;
        QObject::connect(&process, &TreeProcess::completed, &loop, [&](const ProcessResult &r) {
            result = r; received = true;
            if (r.status == ProcessResult::Status::TerminationUnconfirmed) notice("Terminacion no confirmada. Se conserva el lock; espere antes de reintentar o inspeccionar el destino.");
            if (!process.busy()) loop.quit();
        });
        QObject::connect(&process, &TreeProcess::settled, &loop, &QEventLoop::quit);
        QTimer timer; QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (cancelled->load()) process.cancel(); }); timer.start(50);
        require(process.start(cli, args, QFileInfo(cli).absolutePath(), 120000, 1024 * 1024), "No se pudo iniciar la comprobacion CLI");
        loop.exec();
        require(received && result.status == ProcessResult::Status::Success, "Comprobacion CLI fallida: " + result.detail + '\n' + QString::fromUtf8(result.output));
        return result.output;
    }
    PreparationResult prepare(const IntegrationRequest &r, const QString &output) {
        checkpoint(); PreparationService service(r.stagedExecutable); QEventLoop loop; PreparationResult result; bool received = false;
        QObject::connect(&service, &PreparationService::completed, &loop, [&](const PreparationResult &p) {
            result = p; received = true;
            if (p.status == PreparationResult::Status::TerminationUnconfirmed) notice("Preparacion: terminacion/publicacion no confirmada; espere, se conserva el lock.");
            if (!service.busy()) loop.quit();
        });
        QObject::connect(&service, &PreparationService::settled, &loop, [&] { if (received) loop.quit(); });
        QTimer timer; QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (cancelled->load()) service.cancel(); }); timer.start(50);
        require(service.start(r.project.root, r.python, 300000, output), "No se pudo iniciar preparacion Python"); loop.exec();
        require(received && (result.status == PreparationResult::Status::Prepared || result.status == PreparationResult::Status::Reused), result.diagnostic + '\n' + QString::fromUtf8(result.output));
        checkpoint(); return result;
    }
    QJsonObject native(const IntegrationRequest &r) {
        checkpoint(); NativeService service; service.setExecutable(QFileInfo(r.stagedExecutable).absolutePath() + "/ARTestDevNative.exe");
        QEventLoop loop; QJsonObject report; ProcessResult result; bool received = false;
        QObject::connect(&service, &NativeService::completed, &loop, [&](const QJsonObject &j, const ProcessResult &p) {
            report = j; result = p; received = true;
            if (p.status == ProcessResult::Status::TerminationUnconfirmed) notice("Validacion nativa: terminacion no confirmada; lock conservado.");
            if (!service.busy()) loop.quit();
        });
        QObject::connect(&service, &NativeService::settled, &loop, [&] { if (received) loop.quit(); });
        QTimer timer; QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (cancelled->load()) service.cancel(); }); timer.start(50);
        require(service.start(r.project.projectFile, r.configuration, r.target.value("cli").toString()), "Seleccione Debug/Release y un proyecto C++ valido"); loop.exec();
        require(received && result.status == ProcessResult::Status::Success && report.value("status") == "target-validated", "Build requerido o salida invalida: compile la configuracion seleccionada en Visual Studio y vuelva a Integrate.\n" + report.value("diagnostic").toString() + '\n' + result.detail);
        checkpoint(); return report;
    }
    void similarities(const QString &catalog, const QString &package, const QString &extension) {
        if (!QFileInfo::exists(catalog)) return;
        try {
            const auto own = load(package + "/artest-extension.json").value("components").toArray();
            require(own.size() <= 256, "Comparacion de nombres incompleta: mas de 256 componentes");
            int comparisons = 0, warnings = 0, packages = 0;
            for (const auto &dir : QDir(catalog).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
                require(++packages <= 1024, "Comparacion de nombres incompleta: limite de paquetes");
                const auto path = catalog + '/' + dir + "/artest-extension.json"; if (!QFileInfo::exists(path)) continue;
                const auto other = load(path); if (other.value("extensionId") == extension) continue;
                for (const auto &a : own) for (const auto &b : other.value("components").toArray()) {
                    require(++comparisons <= 65536 && warnings < 100, "Comparacion de nombres incompleta: limite de avisos/comparaciones");
                    const auto left = a.toObject(), right = b.toObject();
                    if (left.value("typeId") == right.value("typeId") || left.value("kind") != right.value("kind")) continue;
                    const auto reason = nameSimilarity(left.value("displayName").toString(), right.value("displayName").toString());
                    if (!reason.isEmpty()) { ++warnings; notice("AVISO: " + reason + ": " + left.value("displayName").toString() + " / " + right.value("displayName").toString() + " en " + other.value("extensionId").toString() + ". No se cambian IDs ni se seleccionan servicios."); }
                }
            }
        } catch (const std::exception &e) { notice(QString::fromUtf8(e.what())); }
    }
    void discover(const QJsonObject &p, const QString &catalog, const QString &mapping, const QString &extension, const QString &scratch) {
        const auto cli = p.value("cli").toString();
        const auto report = object(run(cli, {"extensions", "validate", catalog}));
        require(report.value("schema") == "artest.schema.extension-catalog.v2" && report.value("valid").toBool() && report.value("abi").toObject() == QJsonObject{{"major", 0}, {"minor", 2}}, "CLI/Engine incompatibles o catalogo invalido");
        int matches = 0; for (const auto &v : report.value("packages").toArray()) { const auto pkg = v.toObject(); if (pkg.value("extensionId") == extension && pkg.value("valid").toBool()) ++matches; }
        require(matches == 1, "El destino no descubre exactamente una revision valida de " + extension);
        const auto plan = scratch + "/discovery.json";
        save(plan, {{"format", "ARTest.Script"}, {"version", 1}, {"instruments", QJsonArray{}}, {"commands", QJsonArray{QJsonObject{{"stepId", 1}, {"name", "Time.WaitMs"}, {"instrument", "NoInstrument"}, {"params", QJsonObject{{"milliseconds", 0}}}}}}});
        run(cli, {"compile", plan, "--extensions", catalog, "--python-environments", mapping});
    }
};
bool overlaps(const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) == 0 || a.startsWith(b + '/', Qt::CaseInsensitive) || b.startsWith(a + '/', Qt::CaseInsensitive); }
IntegrationResult integrate(IntegrationRequest r, Runner &run) {
    IntegrationResult result; result.target = r.target.value("cli").toString(); std::unique_ptr<Transaction> transaction;
    try {
        run.notice("Comprobando instalacion, recursos e inputs…"); run.checkpoint();
        const auto sdk = inspectStagingExecutable(r.stagedExecutable); require(sdk.valid, sdk.diagnostics.join('\n'));
        r.project = inspectProject(r.project.root); require(r.project.valid, r.project.diagnostics.join('\n'));
        checkProfile(r.target);
        const auto cli = absolute(r.target.value("cli").toString()), engine = absolute(r.target.value("engine").toString());
        for (const auto &path : {QFileInfo(cli).absolutePath(), r.target.value("catalog").toString(), r.target.value("configuration").toString()}) {
            require(!overlaps(absolute(path), absolute(r.project.root)) && !overlaps(absolute(path), absolute(sdk.root)), "Instalacion, proyecto y SDK deben permanecer separados");
        }
        const auto help = run.run(cli, {"help"}); require(help.contains("extensions validate") && help.contains("extensions doctor") && help.contains("compile"), "CLI no expone capacidades requeridas; seleccione instalacion compatible");
        const auto cliHash = digest(cli), engineHash = digest(engine);
        transaction = std::make_unique<Transaction>(r.target, r.registryRoot); transaction->recover();
        const auto config = r.target.value("configuration").toString();
        const auto base = config + "/r/" + hash(r.project.extensionId.toUtf8()).left(16); QString package, receipt, revision;
        if (r.project.language == "python") {
            run.notice("Preparando/reutilizando Python en su ruta final de instalacion…"); const auto p = run.prepare(r, base + "/py"); package = p.package; receipt = p.receipt; revision = p.identity;
        } else {
            run.notice("Comprobando salidas actuales del IDE; no se recompila…"); run.native(r);
            const auto source = r.project.root + "/.artest/native/" + r.configuration + "/current/package"; const auto files = inventory(source);
            QStringList lines; for (auto it = files.begin(); it != files.end(); ++it) lines << it.key() + '=' + it.value().toString();
            revision = hash(lines.join('\n').toUtf8()); package = base + '/' + revision + "/package";
            if (!QFileInfo::exists(package)) copy(source, package); else require(inventory(package) == files, "Revision nativa corrupta; conserve y revise " + package);
            require(inventory(source) == files, "Salida del IDE cambio durante integracion; repita tras Build");
        }
        result.revision = revision; run.checkpoint();
        run.similarities(r.target.value("catalog").toString(), package, r.project.extensionId);
        run.notice("Validando catalogo completo y asociaciones…"); transaction->stage(package, receipt, revision, r.project.extensionId, r.project.language);
        const auto scratch = config + "/.artestdev-discovery." + QUuid::createUuid().toString(QUuid::Id128);
        const auto candidateBefore = inventory(transaction->candidate());
        run.discover(r.target, transaction->candidate(), transaction->mapping(), r.project.extensionId, scratch);
        require(inventory(transaction->candidate()) == candidateBefore && digest(cli) == cliHash && digest(engine) == engineHash, "Candidato/runtime cambio durante validacion");
        if (r.project.language == "python") {
            const auto finalInputs = run.prepare(r, base + "/py");
            require(finalInputs.identity == revision, "Las fuentes Python cambiaron durante validacion; seleccione Integrate de nuevo cuando termine de editar");
        } else {
            run.native(r);
            require(inventory(r.project.root + "/.artest/native/" + r.configuration + "/current/package") == inventory(package), "Las salidas del IDE cambiaron durante validacion; vuelva a Integrate");
        }
        run.checkpoint(); run.notice("Publicando y confirmando descubrimiento instalado…"); transaction->promote();
        run.discover(r.target, r.target.value("catalog").toString(), config + "/python-environments.json", r.project.extensionId, scratch);
        require(inventory(r.target.value("catalog").toString()) == candidateBefore && digest(cli) == cliHash && digest(engine) == engineHash, "Resultado instalado cambio durante verificacion");
        run.checkpoint(); const auto warning = transaction->commit(); result.success = true;
        result.diagnostic = "Integrado y descubierto en la instalacion. No se ejecuto un Test plan.\nDestino: " + result.target + "\nRevision: " + revision + (warning.isEmpty() ? QString() : "\n" + warning);
    } catch (const std::exception &e) {
        result.diagnostic = QString::fromUtf8(e.what());
        if (transaction) {
            try { transaction->recover(); result.diagnostic += "\nRecuperacion terminada; seleccion previa conservada salvo una transaccion ya committed. Los archivos de evidencia se retienen."; }
            catch (const std::exception &recovery) { result.diagnostic += "\nRECUPERACION REQUERIDA: " + QString::fromUtf8(recovery.what()) + "\nConserve journal y archivos; no borre locks/recibos ni reintente con otro escritor activo."; }
        }
        result.diagnostic += "\nDestino: " + result.target + "\nRevision: " + result.revision;
    }
    return result;
}
}
IntegrationService::IntegrationService(QObject *parent) : QObject(parent) {
    connect(&worker_, &QFutureWatcher<IntegrationResult>::finished, this, [this] { running_ = false; emit completed(worker_.result()); });
}
// The UI refuses closure while busy; this also protects non-UI owners from a dangling worker.
IntegrationService::~IntegrationService() { cancel(); worker_.waitForFinished(); }
bool IntegrationService::start(const IntegrationRequest &request) {
    if (running_) return false; running_ = true; cancelled_ = std::make_shared<std::atomic_bool>(false);
    auto r = request; if (r.registryRoot.isEmpty()) r.registryRoot = Registration::configurationRoot();
    if (r.stagedExecutable.isEmpty()) r.stagedExecutable = QCoreApplication::applicationFilePath();
    auto cancellation = cancelled_;
    worker_.setFuture(QtConcurrent::run([this, r, cancellation] {
        Runner runner{cancellation, [this](const QString &message) { QMetaObject::invokeMethod(this, [this, message] { emit progress(message); }, Qt::QueuedConnection); }};
        return integrate(r, runner);
    })); return true;
}
void IntegrationService::cancel() { if (cancelled_) cancelled_->store(true); }
}

#include "TestPlanDialog.h"
#include "RegistrationStore.h"
#include "DiagnosticMenu.h"
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtConcurrentRun>
namespace ARTestDev {
TestPlanDialog::TestPlanDialog(const Project &project, const QString &python, QWidget *parent)
    : QDialog(parent), project_(project), python_(python), root_(Registration::configurationRoot()) {
    setWindowTitle("Run Test plan"); resize(850, 650);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Proyecto: " + project.root));
    auto *row = new QHBoxLayout;
    plan_ = new QLineEdit(project.plan); plan_->setObjectName("testPlanPath"); plan_->setReadOnly(true);
    pick_ = new QPushButton("Seleccionar Test plan…"); pick_->setObjectName("selectTestPlan");
    row->addWidget(plan_); row->addWidget(pick_); layout->addLayout(row);
    targets_ = new QComboBox; targets_->setObjectName("testPlanTarget"); layout->addWidget(targets_);
    mode_ = new QComboBox; mode_->setObjectName("testPlanMode"); mode_->addItems({"Revision integrada", "Fuentes locales (catalogo aislado)"}); layout->addWidget(mode_);
    configuration_ = new QComboBox(this); configuration_->setObjectName("testPlanConfiguration"); configuration_->addItems({"Release", "Debug"}); configuration_->setVisible(project.language == "cpp"); layout->addWidget(configuration_);
    timeout_ = new QSpinBox; timeout_->setRange(1, 30); timeout_->setValue(5); timeout_->setPrefix("Limite del supervisor: "); timeout_->setSuffix(" min"); layout->addWidget(timeout_);
    auto *explanation = new QLabel("Se valida una copia exacta del Test plan seleccionado. La revision corresponde al codigo de la extension.\n"
        "Validar no ejecuta pasos. Fuentes locales prepara Python o comprueba el Build C++ existente.\n"
        "Cancelar durante ejecucion termina procesos: no confirma limpieza ni efectos externos.");
    explanation->setWordWrap(true); layout->addWidget(explanation);
    status_ = new QLabel("Seleccione destino y Test plan."); status_->setObjectName("testPlanStatus"); status_->setWordWrap(true); status_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status_);
    log_ = new QPlainTextEdit; log_->setObjectName("testPlanDiagnostics"); log_->setReadOnly(true); log_->document()->setMaximumBlockCount(1500); enableDiagnosticClear(log_); layout->addWidget(log_);
    row = new QHBoxLayout;
    validate_ = new QPushButton("Validar offline"); validate_->setObjectName("validateTestPlan");
    execute_ = new QPushButton("Ejecutar Test plan validado"); execute_->setObjectName("executeTestPlan");
    cancel_ = new QPushButton("Cerrar"); cancel_->setObjectName("cancelTestPlan");
    for (auto *button : {validate_, execute_, cancel_}) row->addWidget(button); layout->addLayout(row);
    connect(pick_, &QPushButton::clicked, this, [this] { const auto p = QFileDialog::getOpenFileName(this, "Test plan", plan_->text(), "Test plans (*.json)"); if (!p.isEmpty()) plan_->setText(p); refresh(); });
    connect(targets_, &QComboBox::currentIndexChanged, this, [this] {
        const auto p = targets_->currentData().toJsonObject();
        status_->setText("Destino: " + p.value("cli").toString() + "\nCatalogo: " + p.value("catalog").toString() + "\nRevision: se verificara al validar offline."); refresh();
    });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(validate_, &QPushButton::clicked, this, [this] {
        TestPlanRequest r; r.project = project_; r.python = python_; r.registryRoot = root_;
        r.target = targets_->currentData().toJsonObject(); r.configuration = configuration_->currentText(); r.sources = mode_->currentIndex() == 1; r.plan = plan_->text();
        r.executionTimeoutMs = timeout_->value() * 60000;
        if (service_.start(r)) { status_->setText("Validando offline…"); refresh(); }
    });
    connect(execute_, &QPushButton::clicked, this, [this] { service_.execute(); status_->setText("Ejecucion solicitada explicitamente…"); refresh(); });
    connect(cancel_, &QPushButton::clicked, this, &TestPlanDialog::reject);
    connect(&service_, &TestPlanService::progress, log_, &QPlainTextEdit::appendPlainText);
    connect(&service_, &TestPlanService::validated, this, [this](const TestPlanResult &r) {
        status_->setText("VALIDADO OFFLINE; aun no ejecutado.\nModo: " + mode_->currentText() + "\nDestino: " + targets_->currentData().toJsonObject().value("cli").toString() + "\nRevision: " + r.revision + "\nEvidencia: " + r.evidence);
        log_->appendPlainText("Validacion correcta. Pulse Ejecutar o Cancelar. La autorizacion expira en 10 minutos."); refresh();
    });
    connect(&service_, &TestPlanService::completed, this, [this](const TestPlanResult &r) {
        const auto processState = r.process.status;
        QString outcome = r.executed ? "Ejecucion finalizada" : "No se ejecuto";
        if (r.executed && (processState == ProcessResult::Status::Cancelled || processState == ProcessResult::Status::Timeout || processState == ProcessResult::Status::OutputLimit || processState == ProcessResult::Status::TerminationUnconfirmed)) outcome = "Ejecucion interrumpida; efectos/limpieza NO confirmados";
        bool uncertain = false;
        for (const auto &step : r.report.value("steps").toArray()) uncertain |= step.toObject().value("outcome").toObject().value("indeterminate").toBool();
        status_->setText(outcome + "\nEngine status: " + r.report.value("status").toString("sin resultado") +
            (uncertain ? "\nEFECTO INDETERMINADO: inspeccione antes de otra ejecucion." : QString()) +
            "\nResumen Engine: " + QString::fromUtf8(QJsonDocument(r.report.value("summary").toObject()).toJson(QJsonDocument::Compact)) +
            "\nRevision: " + r.revision + "\nCLI exit: " + QString::number(r.process.exitCode) + "\nEvidencia: " + r.evidence);
        log_->appendPlainText(r.diagnostic); refresh();
    });
    connect(&discovery_, &QFutureWatcher<QJsonObject>::finished, this, [this] {
        try {
            const auto registry = discovery_.result(); const auto profiles = registry.value("profiles").toObject();
            int selected = -1;
            for (auto it = profiles.begin(); it != profiles.end(); ++it) { auto p = it.value().toObject(); p["name"] = it.key(); targets_->addItem(it.key() + " — " + p.value("cli").toString(), p); if (registry.value("selected") == it.key()) selected = targets_->count() - 1; }
            targets_->setCurrentIndex(selected);
            if (profiles.isEmpty()) log_->appendPlainText("No hay perfil instalado. Use Integrate → To ARTestCLI para seleccionar y registrar un destino.");
        } catch (const std::exception &e) { log_->appendPlainText(QString::fromUtf8(e.what())); }
        refresh();
    });
    const auto root = root_; discovery_.setFuture(QtConcurrent::run([root] { return Registration::registry(root); })); refresh();
}
void TestPlanDialog::refresh() {
    const bool busy = service_.busy() || discovery_.isRunning();
    targets_->setEnabled(!busy); mode_->setEnabled(!busy); configuration_->setEnabled(!busy);
    timeout_->setEnabled(!busy);
    pick_->setEnabled(!busy); validate_->setEnabled(!busy && targets_->currentIndex() >= 0 && !plan_->text().isEmpty());
    execute_->setEnabled(service_.ready()); cancel_->setText(service_.busy() ? "Cancelar y esperar" : "Cerrar");
}
void TestPlanDialog::reject() {
    if (service_.busy()) { service_.cancel(); log_->appendPlainText("Cancelacion solicitada. Espere la salida confirmada; no hay reintento automatico."); refresh(); return; }
    if (!discovery_.isRunning()) QDialog::reject();
}
}

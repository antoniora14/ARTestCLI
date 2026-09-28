#include "IntegrationDialog.h"
#include "DiagnosticMenu.h"
#include "RegistrationStore.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtConcurrentRun>
namespace ARTestDev {
IntegrationDialog::IntegrationDialog(const Project &project, const QString &python, QWidget *parent)
    : QDialog(parent), project_(project), python_(python), root_(Registration::configurationRoot()) {
    setWindowTitle("Integrate → To ARTestCLI"); resize(780, 560);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Proyecto: " + project.root));
    targets_ = new QComboBox; targets_->setObjectName("installationSelection"); layout->addWidget(targets_);
    auto *row = new QHBoxLayout; file_ = new QPushButton("Seleccionar ARTestCLI.exe…"); folder_ = new QPushButton("Seleccionar carpeta…");
    row->addWidget(file_); row->addWidget(folder_); layout->addLayout(row);
    configuration_ = new QComboBox; configuration_->addItems({"Release", "Debug"}); configuration_->setVisible(project.language == "cpp"); layout->addWidget(configuration_);
    log_ = new QPlainTextEdit; log_->setReadOnly(true); log_->document()->setMaximumBlockCount(1500); layout->addWidget(log_);
    enableDiagnosticClear(log_);
    row = new QHBoxLayout; start_ = new QPushButton("Integrate"); cancel_ = new QPushButton("Cerrar"); row->addWidget(start_); row->addWidget(cancel_); layout->addLayout(row);
    connect(file_, &QPushButton::clicked, this, [this] { manual(false); }); connect(folder_, &QPushButton::clicked, this, [this] { manual(true); });
    connect(targets_, &QComboBox::currentIndexChanged, this, [this] {
        if (targets_->currentIndex() >= 0) { const auto p = targets_->currentData().toJsonObject(); log_->appendPlainText("Destino: " + p.value("cli").toString() + "\nCatalogo: " + p.value("catalog").toString() + "\nConfiguracion: " + p.value("configuration").toString()); } refresh();
    });
    connect(cancel_, &QPushButton::clicked, this, &IntegrationDialog::reject);
    connect(start_, &QPushButton::clicked, this, [this] {
        IntegrationRequest request; request.project = project_; request.python = python_; request.registryRoot = root_;
        request.configuration = configuration_->currentText(); request.target = targets_->currentData().toJsonObject();
        if (service_.start(request)) { log_->appendPlainText("Integracion explicita iniciada…"); refresh(); }
    });
    connect(&service_, &IntegrationService::progress, log_, &QPlainTextEdit::appendPlainText);
    connect(&service_, &IntegrationService::completed, this, [this](const IntegrationResult &r) { log_->appendPlainText((r.success ? "CONFIRMADO\n" : "NO CONFIRMADO\n") + r.diagnostic); refresh(); });
    connect(&discovery_, &QFutureWatcher<QList<QJsonObject>>::finished, this, [this] {
        try { for (const auto &profile : discovery_.result()) add(profile); }
        catch (const std::exception &e) { log_->appendPlainText(QString::fromUtf8(e.what())); }
        if (targets_->count() != 1) targets_->setCurrentIndex(-1);
        log_->appendPlainText(targets_->count() > 1 ? "Hay varios destinos; seleccione explicitamente uno." : targets_->count() == 0 ? "No se encontro destino. Seleccione una carpeta o ARTestCLI.exe de una instalacion compatible." : "Destino recuperado. Se comprobara compatibilidad al integrar."); refresh();
    });
    const auto root = root_;
    discovery_.setFuture(QtConcurrent::run([root] { return Registration::candidates(root); })); refresh();
}
void IntegrationDialog::add(const QJsonObject &p) { targets_->addItem(p.value("name").toString() + " — " + p.value("cli").toString(), p); }
void IntegrationDialog::manual(bool directory) {
    const auto path = directory ? QFileDialog::getExistingDirectory(this, "Instalacion ARTestCLI") : QFileDialog::getOpenFileName(this, "ARTestCLI.exe", {}, "ARTestCLI (ARTestCLI.exe)");
    if (path.isEmpty()) return;
    try { add(Registration::manualProfile(path, root_)); targets_->setCurrentIndex(targets_->count() - 1); }
    catch (const std::exception &e) { log_->appendPlainText(QString::fromUtf8(e.what())); }
}
void IntegrationDialog::refresh() {
    const bool busy = service_.busy() || discovery_.isRunning(); targets_->setEnabled(!busy); configuration_->setEnabled(!busy); file_->setEnabled(!busy); folder_->setEnabled(!busy);
    start_->setEnabled(!busy && targets_->currentIndex() >= 0); cancel_->setText(service_.busy() ? "Cancelar y esperar" : "Cerrar");
}
void IntegrationDialog::reject() {
    if (service_.busy()) { service_.cancel(); log_->appendPlainText("Cancelacion solicitada; esperando terminacion y recuperacion confirmadas…"); return; }
    if (discovery_.isRunning()) return;
    QDialog::reject();
}
}

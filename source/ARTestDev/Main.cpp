#include "Inspection.h"
#include "SdkLocation.h"
#include "ProcessAdapter.h"
#include "Readiness.h"
#include "AuthoringWidget.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QStatusBar>
#include <QtConcurrent>

using namespace ARTestDev;

class Window final : public QMainWindow {
public:
    Window() {
        setWindowTitle(QStringLiteral("ARTestDev"));
        resize(960, 760);
        auto *body = new QWidget(this);
        auto *layout = new QVBoxLayout(body);
        auto *form = new QFormLayout;
        auto *projectRow = new QHBoxLayout;
        projectPath_ = new QLineEdit;
        projectPath_->setReadOnly(true);
        auto *chooseProject = new QPushButton(QStringLiteral("Abrir proyecto…"));
        projectRow->addWidget(projectPath_);
        projectRow->addWidget(chooseProject);
        form->addRow(QStringLiteral("Proyecto"), projectRow);
        layout->addLayout(form);
        summary_ = new QLabel(QStringLiteral("Seleccione un proyecto existente. Los recursos SDK se resuelven desde ARTestDev."));
        summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(summary_);
        diagnostics_ = new QPlainTextEdit;
        diagnostics_->setReadOnly(true);
        layout->addWidget(diagnostics_);
        status_ = new QLabel(QStringLiteral("Listo para inspeccionar."));
        layout->addWidget(status_);
        recheck_ = new QPushButton(QStringLiteral("Volver a comprobar"));
        recheck_->setEnabled(false);
        layout->addWidget(recheck_);
        auto *shell = new QWidget;
        auto *shellLayout = new QVBoxLayout(shell);
        pages_ = new QStackedWidget;
        auto *welcome = new QWidget;
        auto *welcomeLayout = new QVBoxLayout(welcome);
        welcomeLayout->addStretch();
        welcomeLayout->addWidget(new QLabel(QStringLiteral("Welcome to ARTestDev")));
        welcomeLayout->addWidget(new QLabel(QStringLiteral("The Art of Testing")));
        welcomeCreate_ = new QPushButton(QStringLiteral("Create your own Driver, Command or both"));
        welcomeLayout->addWidget(welcomeCreate_); welcomeLayout->addStretch();
        authoring_ = new AuthoringWidget;
        pages_->addWidget(welcome); pages_->addWidget(authoring_); pages_->addWidget(body);
        shellLayout->addWidget(pages_); setCentralWidget(shell);
        auto *projectMenu = menuBar()->addMenu(QStringLiteral("Project"));
        newProject_ = projectMenu->addAction(QStringLiteral("New"));
        loadProject_ = projectMenu->addAction(QStringLiteral("Load"));
        auto *integrateMenu = menuBar()->addMenu(QStringLiteral("Integrate"));
        auto *cliAction = integrateMenu->addAction(QStringLiteral("To ARTestCLI"));
        cliAction->setEnabled(false);
        cliAction->setStatusTip(QStringLiteral("Disponible cuando se implemente DEV-01.5."));
        auto *studioAction = integrateMenu->addAction(QStringLiteral("To ARTestStudio"));
        studioAction->setEnabled(false);
        studioAction->setStatusTip(QStringLiteral("Integración futura; sin acciones disponibles."));
        auto *helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
        auto *about = helpMenu->addAction(QStringLiteral("About ARTestDev"));
        connect(about, &QAction::triggered, this, [this] {
            QMessageBox::about(this, QStringLiteral("About ARTestDev"),
                QStringLiteral("ARTestDev\nThe Art of Testing\n\n"
                               "Herramienta para crear y editar proyectos de drivers y comandos Python/C++.\n"
                               "La integración con ARTestCLI todavía no está disponible."));
        });
        menuBar()->hide();
        connect(pages_, &QStackedWidget::currentChanged, this, [this](int page) {
            menuBar()->setVisible(page != 0);
        });
        const auto showNewProject = [this] {
            authoring_->newProject();
            pages_->setCurrentIndex(1);
            authoring_->activate();
        };
        connect(newProject_, &QAction::triggered, this, showNewProject);
        connect(welcomeCreate_, &QPushButton::clicked, this, showNewProject);
        connect(loadProject_, &QAction::triggered, this, [this] {
            const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Load project"));
            if (!folder.isEmpty()) {
                pages_->setCurrentIndex(2);
                openProject(folder);
            }
        });
        connect(authoring_, &AuthoringWidget::busyChanged, this, [this] { updateNavigation(); });
        connect(chooseProject, &QPushButton::clicked, this, [this] {
            const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Abrir proyecto existente"));
            if (!folder.isEmpty()) openProject(folder);
        });
        connect(recheck_, &QPushButton::clicked, this, [this] {
            if (process_.busy() || projectWatcher_.isRunning() || kitWatcher_.isRunning()) return;
            const QString projectPath = projectPath_->text();

            if (!projectPath.isEmpty()) openProject(projectPath);
            openSdk();
        });
        connect(&projectWatcher_, &QFutureWatcher<Project>::finished, this, [this] {
            project_ = projectWatcher_.result();
            if (project_.valid) {
                authoring_->open(project_);
                authoring_->activate();
                pages_->setCurrentIndex(1);
            }
            updateView();
            checkPython();
        });
        connect(&kitWatcher_, &QFutureWatcher<Kit>::finished, this, [this] {
            kit_ = kitWatcher_.result();
            updateView();
            checkPython();
        });
        connect(&process_, &ProcessAdapter::completed, this, [this](const ProcessResult &result) {
            check_.accept(activeRun_, result);
            updateView();
        });
        connect(&process_, &ProcessAdapter::settled, this, [this] { updateView(); });
    }

    void openProject(const QString &path) {
        if (projectWatcher_.isRunning()) return;
        openSdk();
        check_.selectionChanged();
        process_.cancel();
        project_ = {};
        projectPath_->setText(path);
        status_->setText(QStringLiteral("Inspeccionando proyecto…"));
        recheck_->setEnabled(false);
        projectWatcher_.setFuture(QtConcurrent::run([path] { return inspectProject(path); }));
        updateNavigation();
    }
    void openSdk() {
        if (kitWatcher_.isRunning()) return;
        check_.selectionChanged();
        process_.cancel();
        kit_ = {};

        status_->setText(QStringLiteral("Verificando inventario del kit…"));
        recheck_->setEnabled(false);
        kitWatcher_.setFuture(QtConcurrent::run([] { return installedSdk(); }));
        updateNavigation();
    }
protected:
    void closeEvent(QCloseEvent *event) override {
        if (!authoring_->canClose() || projectWatcher_.isRunning() || kitWatcher_.isRunning()) {
            statusBar()->showMessage(QStringLiteral("Espere a que termine la operación antes de cerrar ARTestDev."));
            event->ignore();
        } else event->accept();
    }
private:
    void updateNavigation() {
        const bool busy = authoring_->busy() || projectWatcher_.isRunning() || kitWatcher_.isRunning() || process_.busy();
        newProject_->setEnabled(!busy);
        loadProject_->setEnabled(!busy);
        welcomeCreate_->setEnabled(!busy);
    }
    void updateView() {
        updateNavigation();
        const QString tool = project_.language == QStringLiteral("cpp") ? project_.msbuild : project_.python;
        QStringList summary;
        if (projectPath_->text().isEmpty()) summary << QStringLiteral("Proyecto: sin seleccionar.");
        else if (project_.name.isEmpty()) summary << QStringLiteral("Proyecto: no reconocido.");
        else summary << QStringLiteral("%1 | %2 | %3 | extensión %4")
            .arg(project_.name, project_.language, project_.variant, project_.extensionId);

        if (kit_.version.isEmpty()) summary << QStringLiteral("Kit: no reconocido.");
        else summary << QStringLiteral("Kit %1 | SDK nativo %2").arg(kit_.version, kit_.nativeSdk);
        if (!tool.isEmpty()) summary << QStringLiteral("Herramienta local: %1").arg(tool);
        summary_->setText(summary.join('\n'));
        QStringList lines = project_.diagnostics;
        lines.append(kit_.diagnostics);
        for (const QString &item : project_.prerequisites)
            lines << QStringLiteral("Prerrequisito de autoría: %1").arg(item);
        for (const QString &item : project_.targetDiagnostics)
            lines << QStringLiteral("ARTestCLI (instalación separada): %1").arg(item);
        if (project_.valid)
            lines << QStringLiteral("ARTestCLI: la instalación destino no se valida en DEV-01.1 y no es necesaria para abrir el proyecto.");
        if (project_.valid && kit_.valid)
            lines << QStringLiteral("Configuración portable e inventario local válidos; las herramientas y el destino se comprueban por separado.");
        const CheckResult &check = check_.result();
        if (project_.language == QStringLiteral("python")) {
            switch (check.status) {
            case CheckStatus::Pending: lines << QStringLiteral("Python check: comprobación pendiente; no se ha preparado el entorno."); break;
            case CheckStatus::Ready: lines << QStringLiteral("Python check: correcto (sólo lectura). No se ha preparado el entorno."); break;
            case CheckStatus::Missing: lines << QStringLiteral("Python check: prerrequisito faltante o incompatible."); break;
            case CheckStatus::Failed: lines << QStringLiteral("Python check: falló; no se considera listo."); break;
            case CheckStatus::Blocked: lines << QStringLiteral("Python check: bloqueado; espere o vuelva a comprobar manualmente."); break;
            case CheckStatus::NotRequired: lines << QStringLiteral("Python check: aún no comprobado."); break;
            }
            lines.append(check.diagnostics);
            for (const QString &item : check.targetDiagnostics)
                lines << QStringLiteral("ARTestCLI (instalación separada): %1").arg(item);
        } else if (project_.language == QStringLiteral("cpp")) {
            lines << QStringLiteral("Python: no requerido para inspeccionar este proyecto C++.");
            if (project_.prerequisites.isEmpty() && !project_.msbuild.isEmpty())
                lines << QStringLiteral("MSBuild encontrado; el compilador C++, toolset y Windows SDK aún no se verifican para construir.");
        }
        if (lines.isEmpty()) {
            if (projectPath_->text().isEmpty()) lines << QStringLiteral("Kit válido. Seleccione un proyecto existente para continuar.");

            else if (project_.valid && kit_.valid) lines << QStringLiteral("Configuración e inventario válidos para inspección.");
        }
        diagnostics_->setPlainText(lines.join(QStringLiteral("\n\n")));
        if (process_.terminationUnconfirmed()) status_->setText(QStringLiteral("Terminación no confirmada; nueva comprobación bloqueada."));
        else if (process_.busy()) status_->setText(QStringLiteral("Comprobación en curso; interfaz disponible."));
        else status_->setText(QStringLiteral("Inspección terminada."));
        recheck_->setEnabled(!process_.busy() && !projectWatcher_.isRunning() && !kitWatcher_.isRunning() &&
                             !projectPath_->text().isEmpty());
    }
    void checkPython() {
        if (project_.language != QStringLiteral("python") || project_.root.isEmpty()) return;
        if (!project_.valid || !kit_.valid) {
            check_.set(CheckStatus::Blocked, {QStringLiteral("Abra un proyecto portable válido. Si faltan recursos SDK, repare o reinstale ARTestDev y vuelva a comprobar.")});
        } else if (!QFileInfo(project_.python).isExecutable()) {
            check_.set(CheckStatus::Missing, {QStringLiteral("Falta Python compatible: instale CPython 3.13 Windows x64 con GIL y configure su ruta local; después pulse Volver a comprobar.")});
        } else if (process_.busy()) {
            check_.set(CheckStatus::Blocked, {QStringLiteral("El proceso anterior aún no ha terminado; espere y pulse Volver a comprobar.")});
        } else {
            const quint64 token = check_.begin();
            if (process_.start(project_.python, {QStringLiteral("-I"), QStringLiteral("-B"), kit_.projectTool,
                                                 QStringLiteral("check"), project_.root}, 30000)) activeRun_ = token;
            else check_.set(CheckStatus::Blocked, {QStringLiteral("No se pudo iniciar otra comprobación. Pulse Volver a comprobar después.")});
        }
        updateView();
    }
    QLineEdit *projectPath_ = nullptr;

    QLabel *summary_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *recheck_ = nullptr;
    QPlainTextEdit *diagnostics_ = nullptr;
    QFutureWatcher<Project> projectWatcher_;
    QFutureWatcher<Kit> kitWatcher_;
    Project project_;
    Kit kit_;
    CheckTracker check_;
    quint64 activeRun_ = 0;
    ProcessAdapter process_;
    AuthoringWidget *authoring_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QAction *newProject_ = nullptr, *loadProject_ = nullptr;
    QPushButton *welcomeCreate_ = nullptr;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Window window;
    window.show();
    const QStringList args = app.arguments();
    if (args.size() > 1 && args.at(1) == QStringLiteral("--smoke-ui"))
        QTimer::singleShot(500, &app, [&app] { app.exit(installedSdk().valid ? 0 : 2); });
    return app.exec();
}

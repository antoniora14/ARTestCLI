#include "AuthoringWidget.h"
#include "SdkLocation.h"
#include <QCoreApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QDir>
#include <QRegularExpression>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace ARTestDev {
AuthoringWidget::AuthoringWidget(QWidget *parent, QSettings::Format settingsFormat
#ifdef ARTESTDEV_TESTING
    , const QString &testExecutable
#endif
)
    : QWidget(parent), settings_(settingsFormat, QSettings::UserScope, QStringLiteral("ARTest"), QStringLiteral("ARTestDev")) {
    sdkExecutable_ = QCoreApplication::applicationFilePath();
#ifdef ARTESTDEV_TESTING
    if (!testExecutable.isEmpty()) sdkExecutable_ = testExecutable;
#endif
    auto *layout = new QVBoxLayout(this);
    inputs_ = new QWidget;
    auto *form = new QFormLayout(inputs_);
    name_ = new QLineEdit; name_->setObjectName(QStringLiteral("projectName"));
    language_ = new QComboBox; language_->setObjectName(QStringLiteral("language"));
    language_->addItem(QStringLiteral("Python"), QStringLiteral("python"));
    language_->addItem(QStringLiteral("C++"), QStringLiteral("cpp"));
    variant_ = new QComboBox; variant_->setObjectName(QStringLiteral("variant"));
    variant_->addItem(QStringLiteral("Driver y Command"), QStringLiteral("driver-command"));
    variant_->addItem(QStringLiteral("Driver"), QStringLiteral("driver-only"));
    variant_->addItem(QStringLiteral("Command"), QStringLiteral("command-only"));
    workspace_ = new QLineEdit(settings_.value(QStringLiteral("workspace"), defaultWorkspace()).toString());
    workspace_->setObjectName(QStringLiteral("workspace"));
    auto addFolder = [this, form](const QString &label, QLineEdit *field) {
        auto *row = new QHBoxLayout; auto *button = new QPushButton(QStringLiteral("Elegir carpeta…"));
        row->addWidget(field); row->addWidget(button); form->addRow(label, row);
        connect(button, &QPushButton::clicked, this, [this, field] {
            const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Seleccionar carpeta"), field->text());
            if (!path.isEmpty()) field->setText(path);
        });
    };
    form->addRow(QStringLiteral("Nombre"), name_); form->addRow(QStringLiteral("Lenguaje"), language_);
    form->addRow(QStringLiteral("Componentes"), variant_);
    addFolder(QStringLiteral("Workspace"), workspace_);
    python_ = new QComboBox; python_->setObjectName(QStringLiteral("python"));
    const QString savedPython = settings_.value(QStringLiteral("python")).toString();
    if (!savedPython.isEmpty()) python_->addItem(savedPython);
    manualPython_ = new QPushButton(QStringLiteral("Seleccionar python.exe…"));
    auto *pythonRow = new QHBoxLayout; pythonRow->addWidget(python_, 1); pythonRow->addWidget(manualPython_);
    form->addRow(QStringLiteral("Intérprete (sólo Python)"), pythonRow);
    layout->addWidget(inputs_);
    validation_ = new QLabel; validation_->setWordWrap(true); layout->addWidget(validation_);
    generate_ = new QPushButton(QStringLiteral("Generate")); generate_->setObjectName(QStringLiteral("generate")); layout->addWidget(generate_);
    behavior_ = new QLabel; behavior_->setWordWrap(true); behavior_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(behavior_);
    auto *editRow = new QHBoxLayout;
    editor_ = new QComboBox; editor_->setObjectName(QStringLiteral("editor")); editor_->setMinimumContentsLength(25); editor_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    manualEditor_ = new QPushButton(QStringLiteral("Seleccionar IDE .exe…"));
    edit_ = new QPushButton(QStringLiteral("Edit")); edit_->setObjectName(QStringLiteral("edit"));
    editRow->addWidget(editor_, 1); editRow->addWidget(manualEditor_); editRow->addWidget(edit_); layout->addLayout(editRow);
    recheck_ = new QPushButton(QStringLiteral("Volver a comprobar")); recheck_->setObjectName(QStringLiteral("recheck")); layout->addWidget(recheck_);
    diagnostics_ = new QPlainTextEdit; diagnostics_->setReadOnly(true); diagnostics_->setMaximumBlockCount(2000); layout->addWidget(diagnostics_, 1);
    connect(name_, &QLineEdit::textChanged, this, &AuthoringWidget::refresh);
    connect(workspace_, &QLineEdit::textChanged, this, [this] { settings_.setValue(QStringLiteral("workspace"), workspace_->text()); refresh(); });
    connect(language_, &QComboBox::currentIndexChanged, this, [this] { invalidatePython(); populateEditors(); refresh(); });
    connect(variant_, &QComboBox::currentIndexChanged, this, &AuthoringWidget::refresh);
    connect(python_, &QComboBox::currentIndexChanged, this, [this] {
        invalidatePython();
        if (!python_->currentText().isEmpty()) settings_.setValue(QStringLiteral("python"), python_->currentText());
        refresh();
    });
    connect(editor_, &QComboBox::currentIndexChanged, this, [this] { savePreferences(); refresh(); });
    connect(manualPython_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("CPython 3.13 Windows x64 con GIL"), {}, QStringLiteral("Ejecutable (*.exe)"));
        if (!path.isEmpty()) { python_->addItem(path); python_->setCurrentIndex(python_->count() - 1); settings_.setValue(QStringLiteral("python"), path); probePython(); }
    });
    connect(manualEditor_, &QPushButton::clicked, this, [this] {
        const QString lang = project_.valid ? project_.language : language_->currentData().toString();
        const QString path = QFileDialog::getOpenFileName(this, lang == QStringLiteral("cpp") ? QStringLiteral("Visual Studio: seleccione devenv.exe") : QStringLiteral("Seleccione su editor Python"), {}, QStringLiteral("Ejecutable (*.exe)"));
        if (!path.isEmpty()) {
            if (lang == QStringLiteral("cpp") && QFileInfo(path).fileName().compare(QStringLiteral("devenv.exe"), Qt::CaseInsensitive) != 0) {
                log(QStringLiteral("C++ requiere Visual Studio: seleccione devenv.exe. MSBuild es un compilador, no un IDE.")); return;
            }
            editor_->addItem(path, path); editor_->setCurrentIndex(editor_->count() - 1);
        }
    });
    connect(recheck_, &QPushButton::clicked, this, &AuthoringWidget::recheck);
    connect(generate_, &QPushButton::clicked, this, &AuthoringWidget::generate);
    connect(edit_, &QPushButton::clicked, this, [this] {
        if (busy()) return;
        if (editor_->currentData().toString().isEmpty()) {
            log(QStringLiteral("Seleccione uno de los IDE detectados o elija manualmente su ejecutable."));
            if (editor_->count() <= 1) manualEditor_->click();
            if (editor_->currentData().toString().isEmpty()) return;
        }
        savePreferences();
        const QString executable = editor_->currentData().toString(); const Project project = project_;
        editorWatcher_.setFuture(QtConcurrent::run([executable, project] { return launchEditor(executable, project); })); refresh();
    });
    connect(&editorWatcher_, &QFutureWatcher<QString>::finished, this, [this] {
        const QString error = editorWatcher_.result();
        log(error.isEmpty() ? QStringLiteral("Solicitud de apertura enviada al IDE. Edite:\n%1\nEl IDE permanece abierto al salir de ARTestDev.").arg(behaviorFiles(project_).join('\n')) : error); refresh();
    });
    connect(&kitWatcher_, &QFutureWatcher<Kit>::finished, this, [this] {
        kit_ = kitWatcher_.result();
        log(kit_.valid ? QStringLiteral("Inventario de recursos locales íntegro. Recursos de autoría: %1").arg(kit_.root) : kit_.diagnostics.join('\n'));
        refresh();
    });
    connect(&toolsWatcher_, &QFutureWatcher<Tools>::finished, this, [this] {
        tools_ = toolsWatcher_.result();
        const QString selected = python_->currentText();
        {
            // Repopulating candidates is not a user selection and must not save an intermediate index.
            const QSignalBlocker blocker(python_);
            python_->clear(); python_->addItems(tools_.python);
            if (!selected.isEmpty() && python_->findText(selected) < 0) python_->addItem(selected);
            if (!selected.isEmpty()) python_->setCurrentText(selected);
        }
        invalidatePython();
        if (!python_->currentText().isEmpty()) settings_.setValue(QStringLiteral("python"), python_->currentText());
        populateEditors();
        if (language_->currentData() == QStringLiteral("cpp")) log(tools_.diagnostics.join('\n'));
        probePython(); refresh();
    });
    connect(&createWatcher_, &QFutureWatcher<Creation>::finished, this, [this] {
        creation_ = createWatcher_.result();
        if (!creation_.success) { generating_ = false; showCreation(); return; }
        if (creation_.request.language == QStringLiteral("python")) {
            processUse_ = ProcessUse::Create;
            if (!process_.start(python_->currentText(), pythonCreateArguments(creation_), 30000)) {
                creation_.success = false; creation_.diagnostics << QStringLiteral("No se pudo iniciar project.py; vuelva a comprobar."); generating_ = false; showCreation();
            }
        } else finalize();
        refresh();
    });
    connect(&process_, &ProcessAdapter::completed, this, [this](const ProcessResult &result) {
        if (processUse_ == ProcessUse::Probe) {
            if (activeProbeRevision_ != pythonSelectionRevision_ || activeProbePath_ != python_->currentText()) {
                pythonReady_ = false;
                createAfterProbe_ = false;
                log(QStringLiteral("Comprobación Python obsoleta descartada; vuelva a comprobar la selección actual."));
                refresh(); return;
            }
            pythonReady_ = compatiblePython(result);
            log(pythonReady_ ? QStringLiteral("CPython 3.13 Windows x64 con GIL, venv y ensurepip compatible.") :
                QStringLiteral("Python ausente, incompatible o comprobación fallida. Generate Python requiere CPython 3.13 Windows x64 con GIL, venv y pip. Instálelo manualmente desde https://www.python.org/downloads/windows/ y vuelva a comprobar.\n%1\n%2").arg(result.detail, QString::fromUtf8(result.output)));
            if (createAfterProbe_) {
                createAfterProbe_ = false;
                if (pythonReady_) startCreation();
            }
        } else if (result.status == ProcessResult::Status::Success && result.exitCode == 0) finalize();
        else {
            generating_ = false; creation_.success = false;
            creation_.diagnostics << QStringLiteral("project.py create falló: %1\n%2\nTemporal conservado: %3").arg(result.detail, QString::fromUtf8(result.output), creation_.staging);
            showCreation();
        }
        refresh();
    });
    connect(&process_, &ProcessAdapter::settled, this, &AuthoringWidget::refresh);
    log(QStringLiteral("Generate y Edit no requieren ARTestCLI ni un compilador. El build C++ sin PowerShell llegará en DEV-01.4; esta entrega sólo genera y abre fuentes."));
    refresh();
}
bool AuthoringWidget::busy() const {
    return generating_ || process_.busy() || kitWatcher_.isRunning() || toolsWatcher_.isRunning() || createWatcher_.isRunning() || editorWatcher_.isRunning();
}
bool AuthoringWidget::canClose() const {
    // File workers must finish; read-only probes retain ProcessAdapter's bounded teardown.
    // An unconfirmed create child only owns its unpublished staging directory.
    return !generating_ && !kitWatcher_.isRunning() && !toolsWatcher_.isRunning() &&
           !createWatcher_.isRunning() && !editorWatcher_.isRunning();
}
void AuthoringWidget::log(const QString &text) { if (!text.isEmpty()) diagnostics_->appendPlainText(text); }
CreateRequest AuthoringWidget::request() const {
    return {name_->text(), language_->currentData().toString(), variant_->currentData().toString(), workspace_->text(), kit_};
}
void AuthoringWidget::refresh() {
    const bool active = busy();
    inputs_->setEnabled(!active); recheck_->setEnabled(!active); manualEditor_->setEnabled(!active); editor_->setEnabled(!active);
    python_->setEnabled(!active && language_->currentData() == QStringLiteral("python")); manualPython_->setEnabled(python_->isEnabled());
    QStringList errors = validateForm(request());
    if (language_->currentData() == QStringLiteral("python") && !pythonReady_) errors << QStringLiteral("Intérprete Python pendiente o incompatible: pulse Volver a comprobar.");
    validation_->setText(active ? QStringLiteral("Operación en curso; espere. Los datos se conservan.") : errors.isEmpty() ? QStringLiteral("Destino: %1/%2").arg(workspace_->text(), name_->text()) : errors.join('\n'));
    generate_->setEnabled(!active && errors.isEmpty());
    edit_->setEnabled(!active && project_.valid);
    emit busyChanged(active);
}
void AuthoringWidget::savePreferences() {
    const QString lang = project_.valid ? project_.language : language_->currentData().toString();
    if (!editor_->currentData().toString().isEmpty()) settings_.setValue(QStringLiteral("editor/") + lang, editor_->currentData());
}
void AuthoringWidget::populateEditors() {
    const QString lang = project_.valid ? project_.language : language_->currentData().toString();
    const QString saved = settings_.value(QStringLiteral("editor/") + lang).toString();
    const QSignalBlocker blocker(editor_);
    editor_->clear(); editor_->addItem(QStringLiteral("Seleccione IDE/editor…"), QString());
    QStringList candidates = lang == QStringLiteral("cpp") ? tools_.visualStudios : tools_.pythonEditors;
    if (!saved.isEmpty() && !candidates.contains(saved, Qt::CaseInsensitive)) candidates << saved;
    for (const QString &path : candidates) editor_->addItem(path, path);
    if (!saved.isEmpty()) editor_->setCurrentIndex(editor_->findData(saved));
    else if (candidates.size() == 1) editor_->setCurrentIndex(1);
    if (candidates.isEmpty()) log(QStringLiteral("IDE/editor ausente: elija un ejecutable instalado o instale su IDE manualmente. Esto no bloquea Generate ni abrir proyectos."));
}
void AuthoringWidget::recheck() {
    if (busy()) return;
    invalidatePython();
    const QString executable = sdkExecutable_;
    kitWatcher_.setFuture(QtConcurrent::run([executable] { return inspectStagingExecutable(executable); }));
    const QString visualStudio = settings_.value(QStringLiteral("editor/cpp")).toString();
    toolsWatcher_.setFuture(QtConcurrent::run([visualStudio] { return discoverTools(visualStudio); })); refresh();
}
void AuthoringWidget::invalidatePython() {
    ++pythonSelectionRevision_;
    pythonReady_ = false;
    createAfterProbe_ = false;
}
void AuthoringWidget::probePython() {
    pythonReady_ = false;
    if (language_->currentData() != QStringLiteral("python")) return;
    if (python_->currentText().isEmpty()) { log(QStringLiteral("Falta CPython 3.13 Windows x64 con GIL, venv y pip. Instale manualmente desde https://www.python.org/downloads/windows/ y vuelva a comprobar.")); return; }
    if (!QRegularExpression(QStringLiteral("^python(?:3(?:\\.?13)?)?\\.exe$"), QRegularExpression::CaseInsensitiveOption)
             .match(QFileInfo(python_->currentText()).fileName()).hasMatch()) {
        createAfterProbe_ = false;
        log(QStringLiteral("Seleccione directamente python.exe de CPython 3.13; un launcher, shell o IDE no es el intérprete."));
        refresh(); return;
    }
    const QString interpreter = QFileInfo(python_->currentText()).canonicalFilePath();
    const QString sdk = QFileInfo(sdkExecutable_).absolutePath();
    if (!sdk.isEmpty() && interpreter.startsWith(sdk + '/', Qt::CaseInsensitive)) {
        createAfterProbe_ = false;
        log(QStringLiteral("Seleccione CPython instalado en el equipo fuera del SDK. No se utiliza el intérprete incluido en el kit histórico."));
        refresh(); return;
    }
    processUse_ = ProcessUse::Probe;
    activeProbeRevision_ = pythonSelectionRevision_;
    activeProbePath_ = python_->currentText();
    process_.start(python_->currentText(), {QStringLiteral("-I"), QStringLiteral("-B"), QStringLiteral("-c"), pythonProbeCode()}, 10000); refresh();
}
void AuthoringWidget::generate() {
    if (busy() || !generate_->isEnabled()) return;
    if (language_->currentData() == QStringLiteral("python")) {
        createAfterProbe_ = true;
        probePython();
    } else startCreation();
}
void AuthoringWidget::startCreation() {
    const CreateRequest r = request();
    generating_ = true;
    log(QStringLiteral("Generando %1 / %2 en %3/%4…").arg(r.language, r.variant, r.workspace, r.name));
    createWatcher_.setFuture(QtConcurrent::run([r] { return beginCreation(r); })); refresh();
}
void AuthoringWidget::finalize() {
    // A separate watcher avoids interpreting the publication result as another create request.
    const Creation c = creation_;
    auto *watcher = new QFutureWatcher<Creation>(this);
    connect(watcher, &QFutureWatcher<Creation>::finished, this, [this, watcher] {
        creation_ = watcher->result(); generating_ = false; watcher->deleteLater(); showCreation();
    });
    watcher->setFuture(QtConcurrent::run([c] { return finishCreation(c); }));
}
void AuthoringWidget::showCreation() {
    if (creation_.success) {
        project_ = inspectProject(creation_.destination);
        behavior_->setText(QStringLiteral("Proyecto: %1\nEdite el comportamiento en:\n%2").arg(project_.root, behaviorFiles(project_).join('\n')));
        log(QStringLiteral("Generate completado: %1. Test plan creado; no ejecutado.").arg(project_.root));
        populateEditors();
    } else log(creation_.diagnostics.join('\n'));
    refresh();
}
void AuthoringWidget::open(const Project &project) {
    if (busy()) return;
    project_ = project;
    language_->setCurrentIndex(project.language == QStringLiteral("cpp") ? 1 : 0);
    behavior_->setText(QStringLiteral("Proyecto: %1\nEdite el comportamiento en:\n%2").arg(project_.root, behaviorFiles(project_).join('\n')));
    populateEditors(); refresh();
}
void AuthoringWidget::newProject() {
    if (busy()) return;
    project_ = {};
    creation_ = {};
    name_->clear();
    behavior_->clear();
    diagnostics_->clear();
    populateEditors();
    refresh();
}
void AuthoringWidget::activate() { recheck(); }
}

#pragma once
#include "Authoring.h"
#include <QFutureWatcher>
#include <QSettings>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QLabel;

namespace ARTestDev {
class AuthoringWidget final : public QWidget {
    Q_OBJECT
public:
    explicit AuthoringWidget(QWidget *parent = nullptr, QSettings::Format settingsFormat = QSettings::NativeFormat
#ifdef ARTESTDEV_TESTING
        , const QString &testExecutable = {}
#endif
    );
    bool busy() const;
    bool canClose() const;
    void open(const Project &project);
    void activate();
    void newProject();
signals:
    void busyChanged(bool busy);
private:
    CreateRequest request() const;
    void refresh();
    void recheck();
    void probePython();
    void invalidatePython();
    void generate();
    void startCreation();
    void finalize();
    void showCreation();
    void populateEditors();
    void log(const QString &text);
    void savePreferences();
    QSettings settings_;
    QString sdkExecutable_;
    QLineEdit *name_, *workspace_;
    QComboBox *language_, *variant_, *python_, *editor_;
    QWidget *inputs_;
    QPushButton *generate_, *edit_, *recheck_, *manualEditor_, *manualPython_;
    QPlainTextEdit *diagnostics_;
    QLabel *validation_, *behavior_;
    Kit kit_;
    Tools tools_;
    Project project_;
    Creation creation_;
    bool pythonReady_ = false;
    quint64 pythonSelectionRevision_ = 0;
    quint64 activeProbeRevision_ = 0;
    QString activeProbePath_;
    bool generating_ = false;
    bool createAfterProbe_ = false;
    enum class ProcessUse { Probe, Create } processUse_ = ProcessUse::Probe;
    QFutureWatcher<Kit> kitWatcher_;
    QFutureWatcher<Tools> toolsWatcher_;
    QFutureWatcher<Creation> createWatcher_;
    QFutureWatcher<QString> editorWatcher_;
    ProcessAdapter process_;
};
}

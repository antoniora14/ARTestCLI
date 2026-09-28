#include "AuthoringWidget.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>

static QString normalizedPath(const QString &path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

using namespace ARTestDev;
class UiTests : public QObject {
    Q_OBJECT
private slots:
    void formGenerateEditAndPreferences() {
        const QString staging = qEnvironmentVariable("ARTESTDEV_TEST_STAGING");
        if (staging.isEmpty()) QSKIP("Set ARTESTDEV_TEST_STAGING for the UI generation acceptance test");
        QTemporaryDir settings, workspace;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ARTest", "ARTestDev");
        preferences.setValue("sdk", "Z:/obsolete SDK preference");
        preferences.setValue("unrelated", "preserved");
        preferences.sync();
        AuthoringWidget widget(nullptr, QSettings::IniFormat, staging + "/ARTestDev.exe");
        widget.resize(960, 760); widget.show();
        auto *name = widget.findChild<QLineEdit *>("projectName");
        auto *location = widget.findChild<QLineEdit *>("workspace");
        QVERIFY(!widget.findChild<QLineEdit *>("sdk"));
        auto *language = widget.findChild<QComboBox *>("language");
        auto *generate = widget.findChild<QPushButton *>("generate");
        auto *edit = widget.findChild<QPushButton *>("edit");
        auto *recheck = widget.findChild<QPushButton *>("recheck");
        QTRY_VERIFY_WITH_TIMEOUT(recheck->isEnabled(), 20000);
        QVERIFY(!widget.busy());
        QCOMPARE(normalizedPath(location->text()), normalizedPath(defaultWorkspace())); QVERIFY(!generate->isEnabled()); QVERIFY(!edit->isEnabled());
        language->setCurrentIndex(1); name->setText("UI Project"); location->setText(workspace.path() + "/personal workspace");
        QTest::mouseClick(recheck, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 60000);
        QVERIFY(!widget.busy());
        QVERIFY(generate->isEnabled());
        name->setText("../bad"); QVERIFY(!generate->isEnabled()); name->setText("UI Project"); QVERIFY(generate->isEnabled());
        auto *driverName = widget.findChild<QLineEdit *>("driverName");
        auto *commandName = widget.findChild<QLineEdit *>("commandName");
        auto *extensionId = widget.findChild<QLineEdit *>("extensionId");
        auto *driverId = widget.findChild<QLineEdit *>("driverId");
        auto *commandId = widget.findChild<QLineEdit *>("commandId");
        driverName->setText("Power Supply"); commandName->setText("Power-On");
        QCOMPARE(extensionId->text(), QString("ui-project")); QCOMPARE(driverId->text(), QString("ui-project.power-supply"));
        QCOMPARE(commandId->text(), QString("ui-project.power-on")); QVERIFY(extensionId->isReadOnly());
        driverName->clear(); QVERIFY(!generate->isEnabled()); driverName->setText("Power Supply");
        auto *variant = widget.findChild<QComboBox *>("variant");
        variant->setCurrentIndex(1); QVERIFY(!commandName->isEnabled()); QVERIFY(!commandId->isEnabled());
        variant->setCurrentIndex(2); QVERIFY(!driverName->isEnabled()); QVERIFY(commandName->isEnabled());
        variant->setCurrentIndex(0);
        widget.findChild<QCheckBox *>("advancedIds")->setChecked(true);
        const auto editId = [](QLineEdit *field, const QString &value) {
            field->setFocus(); field->selectAll(); QTest::keyClicks(field, value);
        };
        editId(extensionId, "lab.ui"); QCOMPARE(driverId->text(), QString("lab.ui.power-supply"));
        extensionId->selectAll(); QTest::keyClick(extensionId, Qt::Key_Backspace); QVERIFY(!generate->isEnabled());
        editId(extensionId, "lab.ui");
        editId(commandId, "lab.ui.power-supply"); QVERIFY(!generate->isEnabled());
        editId(commandId, "lab.action"); QVERIFY(generate->isEnabled());
        commandName->setText("Read Value"); QCOMPARE(commandId->text(), QString("lab.action"));
        QTest::mouseClick(widget.findChild<QPushButton *>("resetIds"), Qt::LeftButton);
        QCOMPARE(extensionId->text(), QString("ui-project")); QCOMPARE(commandId->text(), QString("ui-project.read-value"));
        editId(extensionId, "local.0123456789abcdef");
        editId(driverId, "local.0123456789abcdef.driver.simulated-source");
        editId(commandId, "local.0123456789abcdef.command.measure-value");
        QTest::mouseClick(generate, Qt::LeftButton);
        QVERIFY(widget.busy()); QVERIFY(!generate->isEnabled()); QVERIFY(!name->isEnabled());
        QVERIFY(!widget.canClose());
        QTRY_VERIFY_WITH_TIMEOUT(edit->isEnabled(), 60000);
        QVERIFY(!widget.busy());
        QVERIFY(QFileInfo::exists(location->text() + "/UI Project/artest-sdk-project.json"));
        const QString identityPath = location->text() + "/UI Project/artest-sdk-project.json";
        QFile identity(identityPath); QVERIFY(identity.open(QIODevice::ReadOnly)); const auto identityBefore = identity.readAll(); identity.close();
        QCOMPARE(QJsonDocument::fromJson(identityBefore).object().value("extensionId").toString(), QString("local.0123456789abcdef"));
        QCOMPARE(name->text(), QStringLiteral("UI Project")); QVERIFY(!generate->isEnabled());
        QVERIFY(edit->isEnabled());
        QVERIFY(widget.canClose());
        auto *editors = widget.findChild<QComboBox *>("editor");
        editors->addItem("Manual IDE 1", "Z:/Manual IDE 1/devenv.exe");
        editors->addItem("Manual IDE 2", "Z:/Manual IDE 2/devenv.exe");
        editors->setCurrentIndex(editors->count() - 1);
        QTest::mouseClick(edit, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(edit->isEnabled(), 10000);
        QCOMPARE(name->text(), QStringLiteral("UI Project"));
        QVERIFY(edit->isEnabled());
        const QString screenshot = qEnvironmentVariable("ARTESTDEV_UI_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(widget.grab().save(screenshot));
        // Preferences are per-user settings; portable source settings have no machine paths.
        AuthoringWidget reopened(nullptr, QSettings::IniFormat, staging + "/ARTestDev.exe");
        QTRY_VERIFY_WITH_TIMEOUT(reopened.findChild<QPushButton *>("recheck")->isEnabled(), 60000);
        QVERIFY(!reopened.busy());
        QCOMPARE(normalizedPath(reopened.findChild<QLineEdit *>("workspace")->text()), normalizedPath(location->text()));
        QVERIFY(!reopened.findChild<QLineEdit *>("sdk"));
        QCOMPARE(preferences.value("sdk").toString(), QStringLiteral("Z:/obsolete SDK preference"));
        QCOMPARE(preferences.value("unrelated").toString(), QStringLiteral("preserved"));
        reopened.activate();
        QTRY_VERIFY_WITH_TIMEOUT(reopened.findChild<QPushButton *>("recheck")->isEnabled(), 60000);
        QVERIFY(!reopened.busy());
        reopened.open(inspectProject(location->text() + "/UI Project"));
        QCOMPARE(normalizedPath(reopened.findChild<QComboBox *>("editor")->currentData().toString()), normalizedPath(QStringLiteral("Z:/Manual IDE 2/devenv.exe")));
        AuthoringWidget missing(nullptr, QSettings::IniFormat, workspace.path() + "/missing/ARTestDev.exe");
        missing.activate();
        QTRY_VERIFY_WITH_TIMEOUT(!missing.busy(), 60000);
        missing.open(inspectProject(location->text() + "/UI Project"));
        QVERIFY(identity.open(QIODevice::ReadOnly)); QCOMPARE(identity.readAll(), identityBefore);
        QVERIFY(!missing.findChild<QPushButton *>("generate")->isEnabled());
        QVERIFY(missing.findChild<QPushButton *>("edit")->isEnabled());
        QVERIFY(missing.findChild<QPlainTextEdit *>()->toPlainText().contains("Repare o reinstale"));
    }
    void explicitPythonSelectionAndStaleProbe() {
        const QString staging = qEnvironmentVariable("ARTESTDEV_TEST_STAGING");
        const QString compatible = QDir::toNativeSeparators(qEnvironmentVariable("ARTESTDEV_TEST_PYTHON"));
        QVERIFY2(!staging.isEmpty() && QFileInfo(compatible).isFile(), "Configure the acceptance kit and external CPython 3.13 interpreter");
        QTemporaryDir settings, workspace, tools;
        QVERIFY(settings.isValid() && workspace.isValid() && tools.isValid());
        const QString incompatible = tools.path() + "/python.exe";
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath() + "/ARTestDevIncompatiblePython.exe", incompatible));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ARTest", "ARTestDev");
        preferences.setValue("python", incompatible);
        preferences.setValue("sdk", QStringLiteral("Z:/obsolete SDK preference"));
        preferences.setValue("workspace", QDir::toNativeSeparators(workspace.path()));
        preferences.sync();
        QCOMPARE(preferences.value("sdk").toString(), QStringLiteral("Z:/obsolete SDK preference"));
        {
            AuthoringWidget widget(nullptr, QSettings::IniFormat, staging + "/ARTestDev.exe");
            widget.show();
            auto *python = widget.findChild<QComboBox *>("python");
            auto *generate = widget.findChild<QPushButton *>("generate");
            auto *recheck = widget.findChild<QPushButton *>("recheck");
            auto *diagnostics = widget.findChild<QPlainTextEdit *>();
            widget.findChild<QLineEdit *>("projectName")->setText("Python selection");
            QCOMPARE(normalizedPath(python->currentText()), normalizedPath(incompatible));
            QVERIFY(!generate->isEnabled());
            QTest::mouseClick(recheck, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(recheck->isEnabled(), 60000);
            QVERIFY(!generate->isEnabled());
            QVERIFY(diagnostics->toPlainText().contains("[3,12]"));

            python->addItem(compatible);
            python->setCurrentIndex(python->count() - 1);
            QVERIFY(!generate->isEnabled());
            QCOMPARE(normalizedPath(preferences.value("python").toString()), normalizedPath(compatible));
            QTest::mouseClick(recheck, Qt::LeftButton);
            QVERIFY(!generate->isEnabled());
            QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 60000);
            QCOMPARE(normalizedPath(python->currentText()), normalizedPath(compatible));
            QVERIFY(diagnostics->toPlainText().contains("ensurepip compatible"));

            // Supersede a live compatible probe A -> B -> A before delivering its result.
            python->addItem(incompatible);
            const int compatibleIndex = python->currentIndex();
            bool superseded = false;
            const auto connection = connect(&widget, &AuthoringWidget::busyChanged, &widget, [&](bool active) {
                if (!active || superseded) return;
                superseded = true;
                python->setCurrentIndex(python->count() - 1);
                python->setCurrentIndex(compatibleIndex);
            });
            QTest::mouseClick(generate, Qt::LeftButton);
            QVERIFY(superseded);
            QVERIFY(!generate->isEnabled());
            QTRY_VERIFY_WITH_TIMEOUT(recheck->isEnabled(), 15000);
            disconnect(connection);
            QVERIFY(diagnostics->toPlainText().contains("obsoleta descartada"));
            QVERIFY(!generate->isEnabled());
            QVERIFY(!QFileInfo::exists(workspace.path() + "/Python selection"));
            QCOMPARE(normalizedPath(python->currentText()), normalizedPath(compatible));
            QTest::mouseClick(recheck, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 60000);
            QTest::mouseClick(generate, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(widget.findChild<QPushButton *>("edit")->isEnabled(), 60000);
            QVERIFY(QFileInfo::exists(workspace.path() + "/Python selection/src/extension.py"));
        }
        preferences.sync();
        QCOMPARE(preferences.value("sdk").toString(), QStringLiteral("Z:/obsolete SDK preference"));
        QCOMPARE(normalizedPath(preferences.value("python").toString()), normalizedPath(compatible));
        AuthoringWidget reopened(nullptr, QSettings::IniFormat, staging + "/ARTestDev.exe");
        auto *python = reopened.findChild<QComboBox *>("python");
        auto *generate = reopened.findChild<QPushButton *>("generate");
        QCOMPARE(normalizedPath(python->currentText()), normalizedPath(compatible));
        reopened.findChild<QLineEdit *>("projectName")->setText("Python reopened");
        QVERIFY(!generate->isEnabled());
        reopened.activate();
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 60000);
        QCOMPARE(normalizedPath(python->currentText()), normalizedPath(compatible));
    }
};
QTEST_MAIN(UiTests)
#include "UiTests.moc"

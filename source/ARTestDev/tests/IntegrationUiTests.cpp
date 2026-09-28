#include "IntegrationDialog.h"
#include "RegistrationStore.h"
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFileDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
using namespace ARTestDev;
namespace R = ARTestDev::Registration;
class IntegrationUiTests final : public QObject {
    Q_OBJECT
    QByteArray previousRoot_, previousPath_;
    QString root_;
private slots:
    void initTestCase() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); fixture.setAutoRemove(false); root_ = fixture.path();
        previousRoot_ = qgetenv("ARTEST_SDK_CONFIG_ROOT"); previousPath_ = qgetenv("PATH");
        qputenv("ARTEST_SDK_CONFIG_ROOT", (root_ + "/local").toUtf8()); qputenv("PATH", "");
        qInfo().noquote() << "Evidence:" << root_;
    }
    void savedMultipleAndManual() {
        R::write(root_ + "/first/ARTestCLI.exe", "candidate"); R::write(root_ + "/second/ARTestCLI.exe", "candidate"); R::write(root_ + "/manual/ARTestCLI.exe", "candidate");
        auto first = R::manualProfile(root_ + "/first", root_ + "/local"), second = R::manualProfile(root_ + "/second/ARTestCLI.exe", root_ + "/local");
        const auto firstName = first.take("name").toString(), secondName = second.take("name").toString();
        R::save(root_ + "/local/installations.json", {{"schema", "artest.schema.sdk-installations.v1"}, {"schemaVersion", 1}, {"selected", secondName}, {"profiles", QJsonObject{{firstName, first}, {secondName, second}}}});
        const auto before = R::read(root_ + "/local/installations.json"); Project project; project.root = root_ + "/project"; project.valid = true; project.language = "cpp";
        IntegrationDialog dialog(project, {}); dialog.show(); auto *combo = dialog.findChild<QComboBox *>("installationSelection"); QVERIFY(combo);
        QTRY_VERIFY_WITH_TIMEOUT(combo->isEnabled(), 10000); QVERIFY(combo->count() >= 2); QCOMPARE(combo->itemData(0).toJsonObject().value("name").toString(), secondName); QCOMPARE(combo->currentIndex(), -1);
        QPushButton *manual = nullptr, *integrate = nullptr;
        for (auto *button : dialog.findChildren<QPushButton *>()) { if (button->text().startsWith("Seleccionar ARTestCLI.exe")) manual = button; if (button->text() == "Integrate") integrate = button; }
        QVERIFY(manual && integrate); QVERIFY(!integrate->isEnabled());
        QTimer pickerTimer, pickerTimeout; pickerTimer.setInterval(100); pickerTimeout.setSingleShot(true);
        connect(&pickerTimer, &QTimer::timeout, &dialog, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *picker = qobject_cast<QFileDialog *>(widget); picker && picker->isVisible() && picker->parentWidget() == &dialog) {
                auto *fileName = picker->findChild<QLineEdit *>("fileNameEdit"); if (fileName) fileName->setText(root_ + "/manual/ARTestCLI.exe"); QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection); return;
            }
        });
        connect(&pickerTimeout, &QTimer::timeout, &dialog, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *picker = qobject_cast<QFileDialog *>(widget); picker && picker->isVisible() && picker->parentWidget() == &dialog) QMetaObject::invokeMethod(picker, "reject", Qt::QueuedConnection);
        });
        pickerTimer.start(); pickerTimeout.start(5000); manual->click(); pickerTimer.stop(); pickerTimeout.stop(); QVERIFY(combo->currentIndex() >= 0); QCOMPARE(combo->currentData().toJsonObject().value("cli").toString(), root_ + "/manual/ARTestCLI.exe"); QVERIFY(integrate->isEnabled());
        QCOMPARE(R::read(root_ + "/local/installations.json"), before); dialog.close();
    }
    void sameExecutableNeedsProfileChoice() {
        auto registry = R::registry(root_ + "/local"); auto profiles = registry.value("profiles").toObject();
        const auto first = R::manualProfile(root_ + "/first", root_ + "/local"); auto alias = first; alias.remove("name"); alias["catalog"] = root_ + "/alias/catalog"; alias["configuration"] = root_ + "/alias/config";
        profiles["same-cli-other-catalog"] = alias; registry["profiles"] = profiles; R::save(root_ + "/local/installations.json", registry);
        const auto before = R::read(root_ + "/local/installations.json"); qputenv("PATH", (root_ + "/first").toUtf8());
        Project project; project.root = root_ + "/project"; project.valid = true; project.language = "cpp";
        IntegrationDialog dialog(project, {}); dialog.show(); auto *combo = dialog.findChild<QComboBox *>("installationSelection"); QVERIFY(combo);
        QTRY_VERIFY_WITH_TIMEOUT(combo->isEnabled(), 10000); QVERIFY(combo->count() >= 3); const auto count = combo->count();
        QPushButton *manual = nullptr; for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text().startsWith("Seleccionar ARTestCLI.exe")) manual = button; QVERIFY(manual);
        QTimer pickerTimer, pickerTimeout; pickerTimer.setInterval(100); pickerTimeout.setSingleShot(true);
        connect(&pickerTimer, &QTimer::timeout, &dialog, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *picker = qobject_cast<QFileDialog *>(widget); picker && picker->isVisible() && picker->parentWidget() == &dialog) { auto *fileName = picker->findChild<QLineEdit *>("fileNameEdit"); if (fileName) fileName->setText(root_ + "/first/ARTestCLI.exe"); QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection); return; }
        });
        connect(&pickerTimeout, &QTimer::timeout, &dialog, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *picker = qobject_cast<QFileDialog *>(widget); picker && picker->isVisible() && picker->parentWidget() == &dialog) QMetaObject::invokeMethod(picker, "reject", Qt::QueuedConnection);
        });
        pickerTimer.start(); pickerTimeout.start(5000); manual->click(); pickerTimer.stop(); pickerTimeout.stop(); QCOMPARE(combo->count(), count); QCOMPARE(combo->currentIndex(), -1);
        QVERIFY(dialog.findChild<QPlainTextEdit *>()->toPlainText().contains("varios perfiles"));
        for (int i = 0; i < combo->count(); ++i) if (combo->itemData(i).toJsonObject().value("name") == "same-cli-other-catalog") combo->setCurrentIndex(i);
        QCOMPARE(combo->currentData().toJsonObject().value("catalog").toString(), root_ + "/alias/catalog"); QCOMPARE(R::read(root_ + "/local/installations.json"), before);
        dialog.close();
    }
    void clearDiagnostics() {
        Project project; project.root = root_ + "/project"; project.valid = true; project.language = "cpp";
        IntegrationDialog dialog(project, {}); dialog.show();
        auto *combo = dialog.findChild<QComboBox *>("installationSelection");
        auto *log = dialog.findChild<QPlainTextEdit *>(); QVERIFY(combo && log);
        QTRY_VERIFY_WITH_TIMEOUT(combo->isEnabled(), 10000);
        const auto selection = combo->currentIndex();
        const auto profile = R::read(root_ + "/local/installations.json");
        const auto evidence = root_ + "/clear-evidence.log"; R::write(evidence, "retained evidence");
        QList<QPair<QPushButton *, QPair<bool, QString>>> states;
        for (auto *button : dialog.findChildren<QPushButton *>()) states.append({button, {button->isEnabled(), button->text()}});
        log->appendPlainText("old diagnostic\nold progress");
        bool found = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto *action : menu->actions()) if (action->text() == "Clear" && action->isEnabled()) {
                found = true; QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center()); return;
            }
            menu->close();
        });
        QTimer escape; escape.setSingleShot(true);
        connect(&escape, &QTimer::timeout, &dialog, [] { if (auto *popup = QApplication::activePopupWidget()) popup->close(); });
        escape.start(2000);
        const QPoint point(10, 10);
        QContextMenuEvent context(QContextMenuEvent::Mouse, point, log->viewport()->mapToGlobal(point));
        QApplication::sendEvent(log->viewport(), &context); escape.stop();
        QVERIFY(found); QVERIFY(log->toPlainText().isEmpty());
        QTimer::singleShot(0, log, [log] { log->appendPlainText("new progress"); });
        QTRY_COMPARE(log->toPlainText(), QString("new progress"));
        log->appendPlainText("CONFIRMADO\nfinal diagnostic");
        QCOMPARE(log->toPlainText(), QString("new progress\nCONFIRMADO\nfinal diagnostic"));
        QCOMPARE(combo->currentIndex(), selection);
        for (const auto &state : states) { QCOMPARE(state.first->isEnabled(), state.second.first); QCOMPARE(state.first->text(), state.second.second); }
        QCOMPARE(R::read(root_ + "/local/installations.json"), profile); QCOMPARE(R::read(evidence), QByteArray("retained evidence"));
        dialog.close();
    }
    void cleanupTestCase() { qputenv("ARTEST_SDK_CONFIG_ROOT", previousRoot_); qputenv("PATH", previousPath_); }
};
int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs); QApplication app(argc, argv); IntegrationUiTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "IntegrationUiTests.moc"

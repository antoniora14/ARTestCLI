#include "../Main.cpp"
#include <QTest>
#include <QTemporaryDir>
#include <QContextMenuEvent>
#include <QFile>

class MainUiTests final : public QObject {
    Q_OBJECT
    static void chooseFolder(Window &window, QAction *load, const QString &folder) {
        QTimer picker, escape; picker.setInterval(100); escape.setSingleShot(true);
        QObject::connect(&picker, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                auto *dialog = qobject_cast<QFileDialog *>(widget);
                if (!dialog || !dialog->isVisible() || dialog->parentWidget() != &window) continue;
                if (auto *name = dialog->findChild<QLineEdit *>("fileNameEdit")) name->setText(folder);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        });
        QObject::connect(&escape, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible() && dialog->parentWidget() == &window) dialog->reject();
        });
        picker.start(); escape.start(5000); load->trigger(); picker.stop(); escape.stop();
    }
    static bool clearThroughMenu(QPlainTextEdit *panel) {
        bool found = false; QTimer escape; escape.setSingleShot(true);
        QObject::connect(&escape, &QTimer::timeout, panel, [] { if (auto *popup = QApplication::activePopupWidget()) popup->close(); });
        QTimer::singleShot(0, panel, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); if (!menu) return;
            for (auto *action : menu->actions()) if (action->text() == "Clear" && action->isEnabled()) {
                found = true; QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center()); return;
            }
            menu->close();
        });
        escape.start(2000); const QPoint point(10, 10);
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, panel->viewport()->mapToGlobal(point));
        QApplication::sendEvent(panel->viewport(), &event); escape.stop(); return found;
    }
private slots:
    void loadInvalidClearAndSubsequentDiagnostics_data() {
        QTest::addColumn<bool>("useLoad");
        QTest::newRow("new-Load") << true;
        QTest::newRow("new-recheck") << false;
    }
    void loadInvalidClearAndSubsequentDiagnostics() {
        QFETCH(bool, useLoad);
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); fixture.setAutoRemove(false);
        qInfo().noquote() << "Evidence:" << fixture.path();
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path() + "/settings");
        const auto project = fixture.path() + "/invalid"; QVERIFY(QDir().mkpath(project));
        QFile evidence(fixture.path() + "/evidence.log"); QVERIFY(evidence.open(QIODevice::WriteOnly)); evidence.write("preserve log"); evidence.close();
        Window window(QSettings::IniFormat); window.show();
        QAction *load = nullptr; for (auto *action : window.findChildren<QAction *>()) if (action->text() == "Load") load = action;
        QVERIFY(load);
        for (auto *button : window.findChildren<QPushButton *>()) if (button->text().startsWith("Create your own")) button->click();
        QTRY_VERIFY_WITH_TIMEOUT(load->isEnabled(), 20000);
        chooseFolder(window, load, project);
        auto *path = window.findChild<QLineEdit *>("inspectionProjectPath");
        auto *panel = window.findChild<QPlainTextEdit *>("inspectionDiagnostics");
        auto *status = window.findChild<QLabel *>("inspectionStatus");
        auto *summary = window.findChild<QLabel *>("inspectionSummary");
        auto *recheck = window.findChild<QPushButton *>("inspectionRecheck");
        QVERIFY(path && panel && status && summary && recheck);
        QCOMPARE(QDir::fromNativeSeparators(path->text()), project);
        QTRY_VERIFY_WITH_TIMEOUT(recheck->isEnabled(), 20000); QVERIFY(panel->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(panel->toPlainText().contains("falta artest-sdk-project.json"), 10000);
        const auto oldMessages = panel->toPlainText();
        const auto priorStatus = status->text(), priorSummary = summary->text();
        QList<QPair<QAction *, bool>> actionStates; for (auto *action : window.findChildren<QAction *>()) actionStates.append({action, action->isEnabled()});
        QVERIFY(clearThroughMenu(panel)); QVERIFY(panel->toPlainText().isEmpty());
        QCOMPARE(status->text(), priorStatus); QCOMPARE(summary->text(), priorSummary); QVERIFY(recheck->isEnabled());
        QCOMPARE(QDir::fromNativeSeparators(path->text()), project);
        for (const auto &state : actionStates) QCOMPARE(state.first->isEnabled(), state.second);
        // Queued rendering of the current result is not a new user inspection.
        bool refreshed = false;
        QTimer::singleShot(0, &window, [&] { window.updateView(); refreshed = true; });
        QTRY_VERIFY(refreshed);
        QVERIFY(panel->toPlainText().isEmpty()); QCOMPARE(status->text(), priorStatus); QCOMPARE(summary->text(), priorSummary);
        if (useLoad) chooseFolder(window, load, project); else recheck->click();
        QTRY_VERIFY_WITH_TIMEOUT(recheck->isEnabled(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(panel->toPlainText(), oldMessages, 10000);
        QCOMPARE(status->text(), priorStatus); QCOMPARE(summary->text(), priorSummary);
        for (const auto &state : actionStates) QCOMPARE(state.first->isEnabled(), state.second);
        QVERIFY(clearThroughMenu(panel));
        window.updateView(); QVERIFY(panel->toPlainText().isEmpty());
        QFile invalid(project + "/artest-sdk-project.json"); QVERIFY(invalid.open(QIODevice::WriteOnly)); invalid.write("{broken"); invalid.close();
        chooseFolder(window, load, project); QTRY_VERIFY_WITH_TIMEOUT(recheck->isEnabled(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(panel->toPlainText().contains("artest-sdk-project.json") && !panel->toPlainText().contains("falta artest-sdk-project.json"), 10000);
        QCOMPARE(status->text(), priorStatus); QCOMPARE(summary->text(), priorSummary);
        QVERIFY(evidence.open(QIODevice::ReadOnly)); QCOMPARE(evidence.readAll(), QByteArray("preserve log")); evidence.close();
        QVERIFY(clearThroughMenu(panel)); window.updateView();
        QVERIFY(panel->toPlainText().isEmpty()); window.close();
    }
};
int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv); MainUiTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "MainUiTests.moc"
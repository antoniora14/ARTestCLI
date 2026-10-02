#pragma once
#include "TestPlanService.h"
#include <QDialog>
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QSpinBox;
namespace ARTestDev {
class TestPlanDialog final : public QDialog {
    Q_OBJECT
public:
    TestPlanDialog(const Project &project, const QString &python, QWidget *parent = nullptr);
protected:
    void reject() override;
private:
    void refresh();
    Project project_;
    QString python_, root_;
    QComboBox *targets_, *mode_, *configuration_;
    QLineEdit *plan_;
    QSpinBox *timeout_;
    QPlainTextEdit *log_;
    QLabel *status_;
    QPushButton *pick_, *validate_, *execute_, *cancel_;
    QFutureWatcher<QJsonObject> discovery_;
    TestPlanService service_;
};
}

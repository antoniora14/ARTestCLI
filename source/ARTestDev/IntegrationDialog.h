#pragma once
#include "IntegrationService.h"
#include <QDialog>
#include <QFutureWatcher>
class QComboBox;
class QPlainTextEdit;
class QPushButton;
namespace ARTestDev {
class IntegrationDialog final : public QDialog {
    Q_OBJECT
public:
    IntegrationDialog(const Project &project, const QString &python, QWidget *parent = nullptr);
protected:
    void reject() override;
private:
    void manual(bool directory);
    void add(const QJsonObject &profile);
    void refresh();
    Project project_;
    QString python_, root_;
    QComboBox *targets_, *configuration_;
    QPlainTextEdit *log_;
    QPushButton *start_, *file_, *folder_, *cancel_;
    QFutureWatcher<QList<QJsonObject>> discovery_;
    IntegrationService service_;
};
}

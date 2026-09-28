#pragma once
#include <QMenu>
#include <QPlainTextEdit>
#include <memory>

namespace ARTestDev {
inline void enableDiagnosticClear(QPlainTextEdit *panel) {
    panel->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(panel, &QWidget::customContextMenuRequested, panel, [panel](const QPoint &position) {
        std::unique_ptr<QMenu> menu(panel->createStandardContextMenu());
        menu->addSeparator();
        menu->addAction("Clear", panel, &QPlainTextEdit::clear);
        menu->exec(panel->viewport()->mapToGlobal(position));
    });
}
}
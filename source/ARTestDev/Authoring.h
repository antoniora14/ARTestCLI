#pragma once

#include "Inspection.h"
#include "ProcessAdapter.h"
#include <QJsonObject>

namespace ARTestDev {
struct CreateRequest {
    QString name;
    QString language = QStringLiteral("python");
    QString variant = QStringLiteral("driver-command");
    QString workspace;
    Kit kit;
};
struct Creation {
    CreateRequest request;
    QString staging;
    QString destination;
    QString extensionId;
    QString projectFile;
    QStringList diagnostics;
    bool success = false;
};
QString defaultWorkspace();
QStringList validateForm(const CreateRequest &request);
Creation beginCreation(const CreateRequest &request);
QStringList pythonCreateArguments(const Creation &creation);
Creation finishCreation(Creation creation);
QStringList behaviorFiles(const Project &project);
QString pythonProbeCode();
bool compatiblePython(const ProcessResult &result);

struct Tools {
    QStringList python;
    QStringList pythonEditors;
    QStringList visualStudios;
    QStringList diagnostics;
};
Tools discoverTools(const QString &manualVisualStudio = {});
QStringList nativePrerequisites(const QString &visualStudioRoot, const QString &windowsSdkRoot);
QStringList editorArguments(const QString &executable, const Project &project);
QString launchEditor(const QString &executable, const Project &project);
}

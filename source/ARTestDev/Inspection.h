#pragma once

#include <QString>
#include <QStringList>

namespace ARTestDev {
struct Project {
    QString root;
    QString name;
    QString language;
    QString variant;
    QString extensionId;
    QString projectFile;
    QString plan;
    QString python;
    QString msbuild;
    QStringList prerequisites;
    QStringList targetDiagnostics;
    QStringList diagnostics;
    bool valid = false;
};

struct Kit {
    QString root;
    QString version;
    QString nativeSdk;
    QString python;
    QString projectTool;
    QStringList diagnostics;
    bool partial = false;
    bool valid = false;
    enum class Origin { HistoricalKit, DevelopmentStaging } origin = Origin::HistoricalKit;
};

Project inspectProject(const QString &folder);
Kit inspectKit(const QString &folder);
}

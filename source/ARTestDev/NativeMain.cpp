#include "NativeOutputs.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFileInfo>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto a = app.arguments();
        ARTestDev::Native::require(a.size() >= 3, "Usage: ARTestDevNative build|inspect|validate <request.json> [ARTestCLI.exe], or check-project|validate-project <vcxproj> <configuration> <sdk> [ARTestCLI.exe]");
        QJsonObject request;
        const auto op = a.at(1);
        if (op == "ide" || op == "ide-rebuild" || op == "ide-clean") {
            ARTestDev::Native::require(a.size() == 11, "Incomplete IDE invocation");
            const QStringList keys{"project", "sdk", "configuration", "platform", "msbuild", "toolchain", "windowsSdk", "windowsSdkVersion", "targetName"};
            for (qsizetype i = 0; i < keys.size(); ++i) request[keys.at(i)] = a.at(i + 2);
        } else if (op == "check-project" || op == "validate-project") {
            ARTestDev::Native::require(a.size() == (op == "check-project" ? 5 : 6), "Incomplete project inspection");
            ARTestDev::Native::require(a.at(3) == "Debug" || a.at(3) == "Release", "Unsupported configuration");
            const QString state = QFileInfo(a.at(2)).absolutePath() + "/.artest/native/" + a.at(3);
            ARTestDev::Native::require(QFileInfo::exists(state + "/current/inputs.json"), "Missing build; compile in Visual Studio");
            request = ARTestDev::Native::readObject(state + "/current/inputs.json").value("request").toObject();
            request["project"] = a.at(2); request["configuration"] = a.at(3); request["sdk"] = a.at(4);
        } else request = ARTestDev::Native::readObject(a.at(2));
        QJsonObject result;
        if (op == "build" || op == "ide" || op == "rebuild" || op == "ide-rebuild") result = ARTestDev::Native::build(request, op == "rebuild" || op == "ide-rebuild");
        else if (op == "clean" || op == "ide-clean") result = ARTestDev::Native::clean(request);
        else if (op == "inspect" || op == "check-project") result = ARTestDev::Native::inspect(request);
        else if (op == "validate-project") result = ARTestDev::Native::validate(request, a.at(5));
        else if (op == "validate") {
            ARTestDev::Native::require(a.size() == 4, "Explicit separate CLI path required");
            result = ARTestDev::Native::validate(request, a.at(3));
        } else ARTestDev::Native::require(false, "Unknown native operation");
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n'; return 0;
    } catch (const std::exception &e) {
        const QString diagnostic = QString::fromUtf8(e.what());
        const QString status = diagnostic.startsWith("Stale") ? "stale" : diagnostic.startsWith("Missing build") ? "missing" : "failed";
        std::cout << QJsonDocument(QJsonObject{{"status", status}, {"diagnostic", diagnostic}}).toJson(QJsonDocument::Compact).constData() << '\n'; return 1;
    }
}

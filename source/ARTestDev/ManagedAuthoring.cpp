#include "ManagedAuthoring.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <stdexcept>

namespace ARTestDev {
namespace {
QString quoted(const QString &value) {
    const auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(bytes.mid(1, bytes.size() - 2));
}
QString readSource(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        throw std::runtime_error("Managed authoring template cannot be read.");
    return QString::fromUtf8(file.readAll()).replace("\r\n", "\n");
}
void writeSource(const QString &path, const QString &text) {
    QFile file(path);
    const auto bytes = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size() || !file.flush())
        throw std::runtime_error("Managed authoring template cannot be written; staging preserved.");
}
void substitute(QString &text, const QString &before, const QString &after) {
    if (text.count(before) != 1) throw std::runtime_error("Managed authoring requires the matching SDK template.");
    text.replace(before, after);
}
}
void applyManagedAuthoring(const Creation &c) {
    // Only newly generated private staging is adapted. Existing project sources
    // and historical kits never enter this path.
    if (c.request.kit.origin != Kit::Origin::DevelopmentStaging) return;
    const bool driver = c.request.variant != "command-only";
    const bool command = c.request.variant != "driver-only";
    const QString root = c.staging + "/project";
    const QString contract = driver ? c.extensionId + ".contract.simulated-source.v1"
                                    : "com.example.artest.contract.value-source.v1";
    if (c.request.language == "python") {
        const QString path = root + "/src/extension.py";
        QString text = readSource(path);
        text.prepend("try:\n    from artest_sdk import IdentityNamespace\n"
                     "except ImportError as error:\n    raise RuntimeError(\"DEV-01.4-B requires ARTest Python SDK 0.2.1 or compatible; select the current authoring SDK.\") from error\n");
        QString entries;
        if (driver) entries += "\"driver\": " + quoted(c.request.driverId);
        if (command) entries += (driver ? ", " : "") + QString("\"read\": ") + quoted(c.request.commandId);
        substitute(text, "EXTENSION_ID = " + quoted(c.extensionId),
                   "# Managed portable identities: keep anchors when changing presentation.\n"
                   "IDENTITIES = IdentityNamespace(" + quoted(c.extensionId) + ", {" + entries + "})\n"
                   "EXTENSION_ID = IDENTITIES.identity");
        if (driver && command) {
            substitute(text, "DRIVER_ID = " + quoted(c.request.driverId), "DRIVER_ID = IDENTITIES.component(\"driver\")");
            substitute(text, "COMMAND_ID = " + quoted(c.request.commandId), "COMMAND_ID = IDENTITIES.component(\"read\")");
        } else if (driver) {
            substitute(text, "\n        " + quoted(c.request.driverId) + ", SimulatedSource,",
                       "\n        IDENTITIES.component(\"driver\"), SimulatedSource,");
        } else {
            substitute(text, "\n        " + quoted(c.request.commandId) + ", MeasureValue,",
                       "\n        IDENTITIES.component(\"read\"), MeasureValue,");
        }
        // Match the registration terminator, never tokens inside quoted display names.
        if (driver) substitute(text, "simulated=True,\n    )", "simulated=True, description=\"\",\n    )");
        if (command) substitute(text, "requires=(CONTRACT,),\n    )", "requires=(CONTRACT,), description=\"\",\n    )");
        writeSource(path, text);
        return;
    }
    const QString operation = driver ? contract + "/read" : "com.example.artest.instrument.value-source.v1/read";
    const QString owner = command ? "ReadValueCommand.h" : "SimulatedValueSource.h";
    QString behavior = readSource(root + '/' + owner);
    substitute(behavior, "namespace artest_extension\n{", "namespace artest_extension\n{\n"
        "// Shared semantic contract; presentation and component identity do not select a driver.\n"
        "inline constexpr char SourceContract[] = " + quoted(contract) + ";\n"
        "inline constexpr char ReadOperation[] = " + quoted(operation) + ";");
    // Replace call sites, never the freshly inserted declarations.
    if (command) {
        substitute(behavior, "            " + quoted(contract) + ",", "            SourceContract,");
        substitute(behavior, "            " + quoted(operation) + ");", "            ReadOperation);");
    } else substitute(behavior, "            " + quoted(operation) + ",", "            ReadOperation,");
    writeSource(root + '/' + owner, behavior);
    if (driver && command) {
        behavior = readSource(root + "/SimulatedValueSource.h");
        behavior.prepend("#include \"ReadValueCommand.h\"\n");
        substitute(behavior, "            " + quoted(operation) + ",", "            ReadOperation,");
        writeSource(root + "/SimulatedValueSource.h", behavior);
    }
    QString source = "#if !__has_include(<ARTest/Authoring.h>)\n#error DEV-01.4-B requires native SDK 0.4.1 or compatible. Select the current authoring SDK.\n#endif\n#include <ARTest/Authoring.h>\n";
    if (command) source += "#include \"ReadValueCommand.h\"\n";
    if (driver) source += "#include \"SimulatedValueSource.h\"\n";
    source += "#if defined(ARTEST_METADATA_GENERATOR)\n#include <ARTest/MetadataGenerator.h>\n#else\n#include <ARTest/Extension.h>\n#endif\n\n"
              "namespace {\nartest::sdk::Extension DefineExtension()\n{\n    using namespace artest::sdk;\n"
              "    using namespace artest_extension;\n"
              "    // Portable identity authority; retain anchors and initial overrides.\n"
              "    const IdentityNamespace ids{" + quoted(c.extensionId) + ", {";
    if (driver) source += "{\"driver\", " + quoted(c.request.driverId) + "}";
    if (command) source += (driver ? ", " : "") + QString("{\"read\", ") + quoted(c.request.commandId) + "}";
    source += "}};\n    Extension extension{ids.Id(), \"0.1.0\", " + quoted(c.request.name + " extension") + ", \"Example\"};\n\n";
    if (command) source += "    extension.AddCommand<ReadValueCommand>(ids.Command(\"read\", " + quoted(c.request.commandName) + ",\n"
        "        {.schema = Schema::Object().Optional(\"factor\", Schema::Number().Minimum(0).Maximum(10)),\n"
        "         .schemaId = " + quoted(c.extensionId + ".parameters.v1") + ",\n"
        "         .requiredContracts = {SourceContract}, .description = \"\"}));\n\n";
    if (driver) source += "    extension.AddDriver<SimulatedValueSource>(ids.Driver(\"driver\", " + quoted(c.request.driverName) + ",\n"
        "        SourceContract, DriverMode::Simulated,\n"
        "        {.schema = Schema::Object().Optional(\"initialValue\", Schema::Number().Minimum(-1000000).Maximum(1000000)),\n"
        "         .schemaId = " + quoted(c.extensionId + ".configuration.v1") + ", .description = \"\"}));\n\n";
    source += "    return extension;\n}\n}\n\n#if defined(ARTEST_METADATA_GENERATOR)\nARTEST_GENERATE_METADATA(DefineExtension)\n#else\nARTEST_EXPORT_EXTENSION(DefineExtension)\n#endif\n";
    writeSource(root + "/Extension.cpp", source);
}
}

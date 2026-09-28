#pragma once
#include <QString>
#include <QList>

struct IdentityOverride {
    QString label, extension, driver, command;
};
inline QList<IdentityOverride> identityOverrides() {
    const QString driver = "com.example.artest.driver.sim-value-source";
    const QString command = "com.example.artest.command.read-value";
    const QString contract = "com.example.artest.contract.value-source.v1";
    const QString configuration = "com.example.artest.schema.sim-value-source.configuration.v1";
    const QString parameters = "com.example.artest.schema.read-value.parameters.v1";
    return {
        {"repro", "bench-power", command, "bench-power.power-on"},
        {"swap", "com.example.artest.extension.starter", command, driver},
        {"contracts", contract, configuration, parameters},
        {"schemas", configuration, parameters, contract},
        {"extension-component", command, contract, configuration},
        {"prefixes", contract + ".custom", command + ".custom", parameters + ".custom"}
    };
}

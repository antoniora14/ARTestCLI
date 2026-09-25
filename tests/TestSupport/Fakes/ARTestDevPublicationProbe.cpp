#include "NativeOutputs.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto a = app.arguments();
        ARTestDev::Native::require(a.size() == 4, "Expected state, project and revision");
        ARTestDev::Native::publishFixture(a[1], a[2], a[3]);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what(); return 1; }
}

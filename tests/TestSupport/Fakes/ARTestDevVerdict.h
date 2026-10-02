#pragma once
#include <ARTest/Extension.h>
#include <ARTest/Metadata.h>
namespace artest::tests::devverdict {
class Command final : public sdk::Command {
public:
    sdk::Result Execute(const sdk::Parameters &p, sdk::Context &context) override {
        if (p.Optional<bool>("flood", false)) for (int i = 0; i < 1024; ++i) context.Log(sdk::LogLevel::Information, std::string(4096, 'x'));
        return sdk::Result::TestVerdict(p.Get<bool>("pass"), {{"value", 1}}, "com.artest.test.measurement.v1", "Simulated measurement");
    }
};
inline sdk::Extension Define() {
    sdk::Extension extension{"com.artest.test.devverdict", "0.1.0", "Test-only measurement", "ARTest tests"};
    extension.AddCommand<Command>({.id = "com.artest.test.command.devverdict", .name = "Simulated verdict",
        .metadata = {.schema = sdk::Schema::Object().Required("pass", sdk::Schema::Boolean()).Optional("flood", sdk::Schema::Boolean())}});
    return extension;
}
}

#include <ARTest/Command.h>
#include <ARTest/InstrumentDriver.h>
#if defined(ARTEST_METADATA_GENERATOR)
#include <ARTest/MetadataGenerator.h>
#else
#include <ARTest/Extension.h>
#endif

namespace
{
using namespace artest::sdk;
constexpr auto contract = "com.artest.compat.value-source.v1";

class ValueSource final : public InstrumentDriver
{
    int m_value = 0;
    bool m_failRead = false, m_failCleanup = false;
  public:
    ValueSource()
    {
        RegisterOperation("com.artest.compat.read.v1", [this](const Parameters &, Context &) {
            if (m_failRead)
                return Result::Failure(Status::ResourceUnavailable, "COMPAT_SERVICE_FAILURE");
            return Result::WithData({{"value", m_value}});
        });
    }
    Result Initialize(const Parameters &config, Context &context) override
    {
        m_value = config.Get<int>("value");
        m_failRead = config.Optional<bool>("failRead", false);
        m_failCleanup = config.Optional<bool>("failCleanup", false);
        context.Log(LogLevel::Information, "COMPAT_INIT");
        return Result::Success();
    }
    Result Shutdown(Context &context) override
    {
        m_value = 0;
        context.Log(LogLevel::Information, "COMPAT_CLEANUP_ATTEMPT");
        return m_failCleanup ? Result::Failure(Status::ExtensionFailure, "COMPAT_CLEANUP_FAILURE")
                             : Result::Success();
    }
};

class ReadValue final : public Command
{
  public:
    Result Execute(const Parameters &params, Context &context) override
    {
        context.Log(LogLevel::Information, "COMPAT_EXECUTE");
        if (auto waited = context.WaitFor(std::chrono::milliseconds{params.Optional<int>("waitMs", 0)}); !waited)
            return waited;
        auto result = context.CallInstrument(contract, "com.artest.compat.read.v1", Json::object());
        if (!result) return result;
        if (!result.Data()) return Result::Failure(Status::HostFailure, "COMPAT_MISSING_DATA");
        const Parameters data{*result.Data()};
        return Result::Success("COMPAT_VALUE=" + std::to_string(data.Get<int>("value")));
    }
};

Extension DefineExtension()
{
    // No component is constructed by generation or descriptor discovery.
    Extension extension{"com.artest.compat.extension", "1.0.0", "Native compatibility fixture", "ARTest"};
    extension.AddCommand<ReadValue>({
        .id = "com.artest.compat.command.read", .name = "Read compatibility value",
        .metadata = {
            .schema = Schema::Object().Optional("waitMs", Schema::Integer().Minimum(0).Maximum(5000)),
            .requiredContracts = {contract}}});
    extension.AddDriver<ValueSource>({
        .id = "com.artest.compat.driver.value", .name = "Compatibility value source",
        .contract = contract, .mode = DriverMode::Simulated,
        .metadata = {.schema = Schema::Object().Required("value", Schema::Integer().Minimum(0).Maximum(100))
            .Optional("failRead", Schema::Boolean()).Optional("failCleanup", Schema::Boolean())}});
    return extension;
}
}

#if defined(ARTEST_METADATA_GENERATOR)
ARTEST_GENERATE_METADATA(DefineExtension)
#else
ARTEST_EXPORT_EXTENSION(DefineExtension)
#endif

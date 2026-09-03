#include "../source/ARTest.SDK/examples/ARTestSdkExample/ReadVoltageCommand.h"
#include "../source/ARTest.SDK/examples/ARTestSdkExample/SimulatedSupplyDriver.h"
#include <ARTest/Extension.h>
#include <ARTest/Testing.h>
#include <gtest/gtest.h>
#include <limits>

using namespace artest::sdk;
namespace sdk_testing = artest::sdk::testing;

TEST(SdkParametersTests, ReportsTheMissingParameterByName)
{
    const Json values = Json::object();
    try
    {
        (void)Parameters{values}.Get<int>("channel");
        FAIL();
    }
    catch (const std::invalid_argument &error)
    {
        EXPECT_NE(std::string{error.what()}.find("channel"), std::string::npos);
    }
}
TEST(SdkParametersTests, RejectsSilentNumericBooleanAndStringCoercion)
{
    const Json values = {{"fraction", 1.0}, {"flag", true}, {"text", "2"}, {"number", 2}};
    const Parameters parameters{values};
    EXPECT_THROW((void)parameters.Get<int>("fraction"), std::invalid_argument);
    EXPECT_THROW((void)parameters.Get<int>("flag"), std::invalid_argument);
    EXPECT_THROW((void)parameters.Get<int>("text"), std::invalid_argument);
    EXPECT_THROW((void)parameters.Get<bool>("number"), std::invalid_argument);
    EXPECT_THROW((void)parameters.Get<std::string>("number"), std::invalid_argument);
}
TEST(SdkParametersTests, ChecksSignedUnsignedNarrowingAndFiniteNumbers)
{
    const Json values = {{"maximum", (std::numeric_limits<std::uint64_t>::max)()},
                         {"negative", -1},
                         {"byte", 256},
                         {"infinity", std::numeric_limits<double>::infinity()},
                         {"valid", 12}};
    const Parameters parameters{values};
    EXPECT_EQ(parameters.Get<std::uint64_t>("maximum"),
              (std::numeric_limits<std::uint64_t>::max)());
    EXPECT_THROW((void)parameters.Get<std::int64_t>("maximum"), std::invalid_argument);
    EXPECT_THROW((void)parameters.Get<unsigned>("negative"), std::invalid_argument);
    EXPECT_THROW((void)parameters.Get<std::uint8_t>("byte"), std::invalid_argument);
    EXPECT_THROW((void)parameters.Get<double>("infinity"), std::invalid_argument);
    EXPECT_EQ(parameters.Get<double>("valid"), 12.0);
}
TEST(SdkParametersTests, DefaultsApplyOnlyToAbsentValues)
{
    const Json values = {{"explicitNull", nullptr}, {"present", 3}};
    const Parameters parameters{values};
    EXPECT_EQ(parameters.Optional<int>("absent", 7), 7);
    EXPECT_EQ(parameters.Optional<int>("present", 7), 3);
    EXPECT_THROW((void)parameters.Optional<int>("explicitNull", 7), std::invalid_argument);
    EXPECT_TRUE(parameters.Get<Json>("explicitNull").is_null());
}
TEST(SdkParametersTests, RejectsNonObjectParameterContainers)
{
    const Json values = Json::array({1, 2});
    EXPECT_THROW((void)Parameters{values}, std::invalid_argument);
}
TEST(SdkResultTests, KeepsSuccessFailureAndPayloadExplicit)
{
    EXPECT_TRUE(Result::Success());
    EXPECT_FALSE(Result::Success().Data());
    EXPECT_EQ((*Result::Success("done").Data())["message"], "done");
    EXPECT_EQ(Result::Success("done").Message(), "done");
    EXPECT_EQ(Result::WithData({{"message", "measured"}, {"voltage", 12}}).Message(), "measured");
    EXPECT_TRUE(Result::WithData({{"voltage", 12}}).Message().empty());
    const auto failure = Result::Failure(Status::ResourceUnavailable, "Device unavailable.");
    EXPECT_FALSE(failure);
    EXPECT_EQ(failure.Code(), Status::ResourceUnavailable);
    EXPECT_EQ(failure.Message(), "Device unavailable.");
    EXPECT_THROW((void)Result::Failure(Status::Ok, "not a failure"), std::invalid_argument);
    EXPECT_THROW((void)Result::Failure(static_cast<Status>(99), "unknown"), std::invalid_argument);
    EXPECT_THROW((void)Result::WithData(Json::array()), std::invalid_argument);
    EXPECT_THROW((void)Result::WithData({{"message", 42}}), std::invalid_argument);
}
TEST(SdkAuthoringTests, CommandBehaviorIsTestableWithoutEngineOrDll)
{
    artest::examples::ReadVoltageCommand command;
    sdk_testing::TestContext context;
    context.instrumentId = "PS1";
    context.onCall = [](const sdk_testing::TestContext::ServiceCall &call) {
        EXPECT_EQ(call.contract, "artest.contract.instrument.power-supply.v1");
        EXPECT_EQ(call.instance, "PS1");
        EXPECT_EQ(call.operation, "artest.instrument.power-supply.v1/read-state");
        EXPECT_EQ(call.request["channel"], 2);
        return Result::WithData({{"voltage", 3.3}});
    };
    const Json input = {{"channel", 2}, {"settleMs", 5}};
    ASSERT_TRUE(command.Validate(Parameters{input}));
    const auto result = command.Execute(Parameters{input}, context);
    ASSERT_TRUE(result);
    EXPECT_EQ((*result.Data())["message"], "Measured 3.300000 V.");
    EXPECT_EQ(context.elapsed.count(), 5);
    ASSERT_EQ(context.calls.size(), 1U);
    ASSERT_EQ(context.logs.size(), 1U);
}
TEST(SdkAuthoringTests, CancellationAndTimeoutPreventServiceCalls)
{
    artest::examples::ReadVoltageCommand command;
    sdk_testing::TestContext context;
    context.instrumentId = "PS1";
    context.cancelled = true;
    const Json input = {{"channel", 1}, {"settleMs", 5}};
    EXPECT_EQ(command.Execute(Parameters{input}, context).Code(), Status::Cancelled);
    EXPECT_TRUE(context.calls.empty());
    context.cancelled = false;
    context.deadline = std::chrono::milliseconds{3};
    EXPECT_EQ(command.Execute(Parameters{input}, context).Code(), Status::TimedOut);
    EXPECT_TRUE(context.calls.empty());
}
TEST(SdkAuthoringTests, MissingBindingAndServiceFailureRemainFailures)
{
    artest::examples::ReadVoltageCommand command;
    sdk_testing::TestContext context;
    const Json input = {{"channel", 1}};
    EXPECT_EQ(command.Execute(Parameters{input}, context).Code(), Status::InvalidArgument);
    context.instrumentId = "PS1";
    context.onCall = [](const auto &) {
        return Result::Failure(Status::ResourceUnavailable, "Offline");
    };
    const auto result = command.Execute(Parameters{input}, context);
    EXPECT_EQ(result.Code(), Status::ResourceUnavailable);
    EXPECT_EQ(result.Message(), "Offline");
}
TEST(SdkAuthoringTests, DriverOperationsAreRegisteredAndTestableLocally)
{
    artest::examples::SimulatedSupplyDriver driver;
    sdk_testing::TestContext context;
    const Json config = {{"voltage", 5.0}}, parameters = {{"channel", 1}};
    ASSERT_TRUE(driver.Initialize(Parameters{config}, context));
    const auto result = driver.Dispatch("artest.instrument.power-supply.v1/read-state",
                                        Parameters{parameters}, context);
    ASSERT_TRUE(result);
    EXPECT_EQ((*result.Data())["voltage"], 5.0);
    EXPECT_EQ(driver.Dispatch("unknown", Parameters{parameters}, context).Code(),
              Status::OperationNotSupported);
    EXPECT_TRUE(driver.Shutdown(context));
}
TEST(SdkAuthoringTests, RejectsDuplicateAndReservedOperationRegistration)
{
    class Driver final : public InstrumentDriver
    {
      public:
        using InstrumentDriver::RegisterOperation;
        Result Initialize(const Parameters &, Context &) override
        {
            return Result::Success();
        }
        Result Shutdown(Context &) override
        {
            return Result::Success();
        }
    } driver;
    const auto handler = [](const Parameters &, Context &) { return Result::Success(); };
    driver.RegisterOperation("test/read", handler);
    EXPECT_THROW(driver.RegisterOperation("test/read", handler), std::invalid_argument);
    EXPECT_THROW(driver.RegisterOperation("artest.lifecycle.shutdown.v1", handler),
                 std::invalid_argument);
    EXPECT_THROW(driver.RegisterOperation("", handler), std::invalid_argument);
    EXPECT_THROW(driver.RegisterOperation("test/empty", {}), std::invalid_argument);
}

#include "TestSupport/ReferenceCatalog.h"
#include "TestSupport/C01/Catalog.h"
#include <ARTestEngineClient.h>
#include <gtest/gtest.h>

using artest::sdk::EngineClient;
using artest::sdk::Json;
namespace effects = artest::tests::effects;
namespace
{
std::filesystem::path Binaries()
{
    wchar_t buffer[32768]{};
    GetModuleFileNameW(nullptr, buffer, 32768);
    return std::filesystem::path(buffer).parent_path();
}
class EffectUncertaintyTests : public ::testing::Test
{
  protected:
    std::unique_ptr<artest::tests::ReferenceCatalog> catalog;
    EngineClient client;
    std::filesystem::path effect;
    void SetUp() override
    {
        const auto bin = Binaries();
        auto root = bin;
        for (int i = 0; i < 4; ++i) root = root.parent_path();
        catalog = std::make_unique<artest::tests::ReferenceCatalog>(
            root / "artifacts/extensions/x64" / bin.filename());
        effects::AddPackage(catalog->Root(), bin);
        effect = catalog->Root() / "effect.txt";
        ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false,"resultSchemaVersion":2})").Succeeded());
        ASSERT_TRUE(client.PrepareCatalog(catalog->Root().string()).Succeeded());
    }
    Json Run(const Json &plan, bool cancelAfterWrite = false)
    {
        auto status = client.Compile(plan.dump());
        EXPECT_TRUE(status.Succeeded()) << status.message;
        if (!status.Succeeded()) return Json::object();
        status = client.Start();
        EXPECT_TRUE(status.Succeeded()) << status.message;
        if (!status.Succeeded()) return Json::object();
        if (cancelAfterWrite)
        {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!std::filesystem::exists(effect) && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            EXPECT_TRUE(std::filesystem::exists(effect));
            client.RequestCancel();
        }
        bool completed = false;
        EXPECT_TRUE(client.Wait(15000, completed).Succeeded());
        EXPECT_TRUE(completed);
        std::string text;
        EXPECT_TRUE(client.SerializeResult(text).Succeeded());
        return Json::parse(text);
    }
    void AssertStopped(const Json &result)
    {
        ASSERT_EQ(result.at("steps").size(), 1u) << result.dump(2);
        EXPECT_EQ(result["summary"]["totalAttempts"], 1);
        EXPECT_EQ(result["summary"]["skippedSteps"], 1);
        EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], true);
        EXPECT_EQ(result["steps"][0]["attempts"][0]["outcome"]["indeterminate"], true);
        EXPECT_NE(result.dump().find("C01 native lost acknowledgement"), std::string::npos);
        EXPECT_EQ(effects::Read(effect), "effect\n");
        EXPECT_EQ(effects::Read(effect.string() + ".calls"), "call\n");
        EXPECT_EQ(effects::Read(effect.string() + ".cleanup"), "cleanup\n");
    }
};
}
TEST_F(EffectUncertaintyTests, NativeLostAckStopsRetriesAndContinuation)
{
    const auto result = Run(effects::Plan(effect));
    EXPECT_EQ(result["status"], "error");
    AssertStopped(result);
}
TEST_F(EffectUncertaintyTests, WrappingTheServiceErrorPreservesItsOrigin)
{
    const auto result = Run(effects::Plan(effect, "lost", "wrap"));
    AssertStopped(result);
    EXPECT_NE(result.dump().find("C01 wrapped command failure"), std::string::npos);
}
TEST_F(EffectUncertaintyTests, CommandExceptionCannotEraseUncertainty)
{
    const auto result = Run(effects::Plan(effect, "lost", "throw"));
    AssertStopped(result);
    EXPECT_NE(result.dump().find("C01 command threw after service"), std::string::npos);
}
TEST_F(EffectUncertaintyTests, BrokerBlocksReplayEvenIfCommandReturnsSuccess)
{
    const auto result = Run(effects::Plan(effect, "lost", "retry"));
    EXPECT_EQ(result["status"], "error");
    AssertStopped(result);
}
TEST_F(EffectUncertaintyTests, ExplicitTimeoutAndCancellationRetainUncertainty)
{
    for (const auto mode : {"timeout", "cancelled"})
    {
        effect = catalog->Root() / (std::string(mode) + ".txt");
        const auto result = Run(effects::Plan(effect, mode));
        EXPECT_EQ(result["status"], std::string(mode) == "timeout" ? "timedOut" : "cancelled");
        AssertStopped(result);
    }
}
TEST_F(EffectUncertaintyTests, DeadlineAfterEffectDoesNotEraseLostAcknowledgement)
{
    auto plan = effects::Plan(effect, "late");
    plan["commands"][0]["policy"]["timeoutMs"] = 50;
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "timedOut");
    AssertStopped(result);
}
TEST_F(EffectUncertaintyTests, UserCancellationAfterWriteRetainsUncertainty)
{
    const auto result = Run(effects::Plan(effect, "hold"), true);
    EXPECT_EQ(result["status"], "cancelled");
    AssertStopped(result);
}
TEST_F(EffectUncertaintyTests, ConfirmedPreSendFailureKeepsNormalRetryAndContinuePolicy)
{
    const auto result = Run(effects::Plan(effect, "before"));
    EXPECT_EQ(result["summary"]["totalAttempts"], 6);
    EXPECT_EQ(result["summary"]["executedSteps"], 2);
    EXPECT_FALSE(std::filesystem::exists(effect));
    EXPECT_EQ(effects::Read(effect.string() + ".calls"), "call\ncall\ncall\ncall\ncall\ncall\n");
    EXPECT_EQ(result["steps"][0]["outcome"]["indeterminate"], false);
    EXPECT_EQ(effects::Read(effect.string() + ".cleanup"), "cleanup\n");
}
TEST_F(EffectUncertaintyTests, CleanupFailurePreservesBothDiagnostics)
{
    auto plan = effects::Plan(effect);
    plan["instruments"][0]["config"]["failShutdown"] = true;
    const auto result = Run(plan);
    AssertStopped(result);
    EXPECT_NE(result.dump().find("C01 cleanup failed"), std::string::npos);
}
TEST_F(EffectUncertaintyTests, NewSessionAndOtherInstancesAreNotContaminated)
{
    AssertStopped(Run(effects::Plan(effect)));
    const auto first = catalog->Root() / "fresh1.txt";
    const auto second = catalog->Root() / "fresh2.txt";
    auto plan = effects::Plan(first, "ok");
    auto instrument = plan["instruments"][0];
    instrument["id"] = "PS2";
    instrument["config"]["effectFile"] = second.string();
    plan["instruments"].push_back(instrument);
    plan["commands"][1]["instrument"] = "PS2";
    const auto result = Run(plan);
    EXPECT_EQ(result["status"], "passed") << result.dump(2);
    for (const auto &step : result["steps"]) EXPECT_EQ(step["outcome"]["indeterminate"], false);
    EXPECT_EQ(effects::Read(first), "effect\n");
    EXPECT_EQ(effects::Read(second), "effect\n");
    EXPECT_EQ(effects::Read(first.string() + ".cleanup"), "cleanup\n");
    EXPECT_EQ(effects::Read(second.string() + ".cleanup"), "cleanup\n");
}
TEST_F(EffectUncertaintyTests, LegacyResultV1RetainsItsShapeAndFailureDiagnostic)
{
    ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    ASSERT_TRUE(client.PrepareCatalog(catalog->Root().string()).Succeeded());
    const auto result = Run(effects::Plan(effect));
    EXPECT_EQ(result["status"], "error");
    EXPECT_EQ(result["summary"]["totalAttempts"], 1);
    EXPECT_FALSE(result["steps"][0].contains("outcome"));
    EXPECT_NE(result.dump().find("EXTENSION_OUTCOME_INDETERMINATE"), std::string::npos);
}

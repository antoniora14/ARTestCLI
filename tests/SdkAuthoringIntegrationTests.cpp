#define WIN32_LEAN_AND_MEAN
#include "../source/ARTestCLI/CliApplication.h"
#include <ARTest/Extension.h>
#include <ARTestEngineClient.h>
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

using artest::sdk::EngineClient;
using artest::sdk::Json;

namespace
{
std::filesystem::path BinaryDirectory()
{
    wchar_t buffer[32768]{};
    GetModuleFileNameW(nullptr, buffer, 32768);
    return std::filesystem::path{buffer}.parent_path();
}
std::filesystem::path Root()
{
    auto path = BinaryDirectory();
    for (unsigned i = 0; i < 4; ++i)
        path = path.parent_path();
    return path;
}
std::filesystem::path Packages()
{
    const auto bin = BinaryDirectory();
    return Root() / "artifacts" / "sdk-examples" / bin.parent_path().filename() / bin.filename();
}
std::filesystem::path ExampleDirectory()
{
    return Root() / "source/ARTest.SDK/examples/ARTestSdkExample";
}
std::string ReadText(const std::filesystem::path &path)
{
    std::ifstream input{path};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
Json Plan()
{
    return Json::parse(ReadText(ExampleDirectory() / "ExamplePlan.json"));
}
Json Finish(EngineClient &client)
{
    bool completed = false;
    const auto waited = client.Wait(10000, completed);
    EXPECT_TRUE(waited.Succeeded()) << waited.message;
    EXPECT_TRUE(completed);
    std::string text;
    const auto serialized = client.SerializeResult(text);
    EXPECT_TRUE(serialized.Succeeded()) << serialized.message;
    return text.empty() ? Json::object() : Json::parse(text);
}
} // namespace
TEST(SdkAuthoringIntegrationTests, ExampleDllRunsThroughTheUnmodifiedEngine)
{
    // The example must remain self-describing: a clean checkout has no hand-maintained
    // manifest/schema fallback that could hide a broken build-time generator.
    EXPECT_FALSE(std::filesystem::exists(ExampleDirectory() / "artest-extension.json"));
    const auto packaged = Json::parse(ReadText(Packages() / "ARTestSdkExample/artest-extension.json"));
    for (const auto &component : packaged["components"])
        for (const auto &schema : component["schemas"])
            EXPECT_FALSE(ReadText(Packages() / "ARTestSdkExample" /
                                  schema["path"].get<std::string>()).empty());
    EngineClient client;
    ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    const auto prepared = client.PrepareCatalog(Packages().string());
    ASSERT_TRUE(prepared.Succeeded()) << prepared.message;
    const auto compiled = client.Compile(Plan().dump());
    ASSERT_TRUE(compiled.Succeeded()) << compiled.message;
    const auto started = client.Start();
    ASSERT_TRUE(started.Succeeded()) << started.message;
    const auto result = Finish(client);
    EXPECT_EQ(result["status"], "passed");
    EXPECT_EQ(result["summary"]["plannedSteps"], 1);
    EXPECT_EQ(result["summary"]["passedSteps"], 1);
    EXPECT_NE(result.dump().find("Measured 12.000000 V."), std::string::npos);
}
TEST(SdkAuthoringIntegrationTests, ExampleDllSupportsFreshSequentialSessions)
{
    EngineClient client;
    ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    ASSERT_TRUE(client.PrepareCatalog(Packages().string()).Succeeded());
    ASSERT_TRUE(client.Compile(Plan().dump()).Succeeded());
    ASSERT_TRUE(client.Start().Succeeded());
    EXPECT_EQ(Finish(client)["status"], "passed");
    ASSERT_TRUE(client.Restart().Succeeded());
    const auto second = Finish(client);
    EXPECT_EQ(second["status"], "passed");
    EXPECT_NE(second.dump().find("Measured 12.000000 V."), std::string::npos);
}
TEST(SdkAuthoringIntegrationTests, ExampleSchemasRejectInvalidParametersBeforeExecution)
{
    EngineClient client;
    ASSERT_TRUE(client.Create(R"({"loadDefaultCatalog":false})").Succeeded());
    ASSERT_TRUE(client.PrepareCatalog(Packages().string()).Succeeded());
    auto plan = Plan();
    plan["commands"][0]["params"]["channel"] = 99;
    const auto compiled = client.Compile(plan.dump());
    EXPECT_FALSE(compiled.Succeeded());
    EXPECT_FALSE(client.Start().Succeeded());
}
TEST(SdkAuthoringIntegrationTests, CliCompilesRunsAndCancelsTheSdkExample)
{
    const auto planPath = (ExampleDirectory() / "ExamplePlan.json").string();
    for (const auto &mode : {"compile", "run", "debug"})
    {
        std::istringstream input{"q\n"};
        std::ostringstream output, error;
        artest::cli::CliApplication cli{input, output, error};
        const auto code = cli.Run({mode, planPath, "--extensions", Packages().string()});
        if (std::string_view{mode} == "debug")
        {
            EXPECT_EQ(code, 5);
            EXPECT_NE(output.str().find("CANCELLED"), std::string::npos);
            EXPECT_NE(output.str().find("SDK simulated power supply shut down."),
                      std::string::npos);
        }
        else
            EXPECT_EQ(code, 0) << error.str();
    }
}
TEST(SdkAuthoringIntegrationTests, ExampleHasNoCoreLinkageOrHandwrittenAbiInComponentCode)
{
    const auto project = ReadText(ExampleDirectory() / "ARTestSdkExample.vcxproj");
    EXPECT_EQ(project.find("ProjectReference"), std::string::npos);
    EXPECT_EQ(project.find("ARTestEngine"), std::string::npos);
    for (const auto &file :
         {"ReadVoltageCommand.h", "SimulatedSupplyDriver.h", "ExampleExtension.cpp"})
    {
        const auto text = ReadText(ExampleDirectory() / file);
        ASSERT_FALSE(text.empty());
        EXPECT_EQ(text.find("ARTestEngine.Core"), std::string::npos);
        EXPECT_EQ(text.find("reinterpret_cast"), std::string::npos);
        EXPECT_EQ(text.find("ARTestExtensionApiV0"), std::string::npos);
        EXPECT_EQ(text.find("ARTEST_ABI_CALL"), std::string::npos);
    }
}

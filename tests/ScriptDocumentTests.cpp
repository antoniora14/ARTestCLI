#include "TestSupport/TemporaryScript.h"

#include "ARTestEngine.Core/Parsing/JsonTestPlanParser.h"

#include <gtest/gtest.h>

using namespace artest;

TEST(JsonTestPlanParserTests, LoadsTheVersionedCanonicalFormatIntoTypedModels)
{
    TemporaryScript script{R"({
        "format":"ARTest.Script",
        "version":1,
        "instruments":[{"type":"CAN","id":"CAN1","config":{"hw-rsrc":"FAKE"}}],
        "commands":[{"stepId":7,"name":"CAN.SendMessage","instrument":"CAN1","params":{"channel":0,"id":"0x123","dlc":0,"data":[]}}]
    })"};

    const auto result = JsonTestPlanParser{}.ParseFile(script.Path());

    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.value->instruments.size(), 1U);
    EXPECT_EQ(result.value->instruments.front().id, "CAN1");
    ASSERT_EQ(result.value->steps.size(), 1U);
    EXPECT_EQ(result.value->steps.front().stepId, 7U);
    ASSERT_TRUE(result.value->steps.front().instrumentId.has_value());
    EXPECT_EQ(*result.value->steps.front().instrumentId, "CAN1");
}

TEST(JsonTestPlanParserTests, ConvertsNoInstrumentToAnEmptyOptional)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"({
        "format":"ARTest.Script","version":1,"instruments":[],
        "commands":[{"stepId":1,"name":"Time.WaitMs","instrument":"NoInstrument","params":{"milliseconds":0}}]
    })");

    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.value->steps.size(), 1U);
    EXPECT_FALSE(result.value->steps.front().instrumentId.has_value());
}

TEST(JsonTestPlanParserTests, RejectsTheLegacyArrayRoot)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"([{"name":"Time.WaitMs"}])");

    ASSERT_FALSE(result.Succeeded());
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().code, "SCRIPT_ROOT_INVALID");
}

TEST(JsonTestPlanParserTests, RejectsUnsupportedVersions)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"({
        "format":"ARTest.Script","version":999,"instruments":[],"commands":[]
    })");

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "SCRIPT_VERSION_UNSUPPORTED");
}

TEST(JsonTestPlanParserTests, RejectsMalformedJson)
{
    const auto result = JsonTestPlanParser{}.ParseText("{ incomplete", "bad-script.json");

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "SCRIPT_JSON_INVALID");
    EXPECT_EQ(result.diagnostics.front().location, "bad-script.json");
}

TEST(JsonTestPlanParserTests, ReportsEveryInvalidTopLevelMember)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"({"format":"wrong"})");

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.size(), 4U);
}

TEST(JsonTestPlanParserTests, RejectsWrongFormatTypesWithoutThrowing)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"({
        "format":42,"version":1,"instruments":[],"commands":[]
    })");

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "SCRIPT_FORMAT_INVALID");
}

TEST(JsonTestPlanParserTests, RejectsOutOfRangeVersionsWithoutThrowing)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"({
        "format":"ARTest.Script","version":18446744073709551615,"instruments":[],"commands":[]
    })");

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "SCRIPT_VERSION_UNSUPPORTED");
}

TEST(JsonTestPlanParserTests, ParsesOptionalStepExecutionPolicy)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"({
        "format":"ARTest.Script","version":1,"instruments":[],
        "commands":[{
            "stepId":1,"name":"Time.WaitMs","instrument":"NoInstrument",
            "params":{"milliseconds":0},
            "policy":{"maxAttempts":3,"retryDelayMs":25,"timeoutMs":500,"onFailure":"continue"}
        }]
    })");

    ASSERT_TRUE(result.Succeeded());
    const auto& policy = result.value->steps.front().policy;
    EXPECT_EQ(policy.maxAttempts, 3);
    EXPECT_EQ(policy.retryDelay.count(), 25);
    EXPECT_EQ(policy.timeout.count(), 500);
    EXPECT_EQ(policy.onFailure, FailureAction::Continue);
}

TEST(JsonTestPlanParserTests, LegacyStepsReceiveSafeDefaultPolicy)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"({
        "format":"ARTest.Script","version":1,"instruments":[],
        "commands":[{"stepId":1,"name":"Time.WaitMs","instrument":"NoInstrument","params":{"milliseconds":0}}]
    })");

    ASSERT_TRUE(result.Succeeded());
    const auto& policy = result.value->steps.front().policy;
    EXPECT_EQ(policy.maxAttempts, 1);
    EXPECT_EQ(policy.retryDelay.count(), 0);
    EXPECT_EQ(policy.timeout.count(), 0);
    EXPECT_EQ(policy.onFailure, FailureAction::Stop);
}

TEST(JsonTestPlanParserTests, RejectsInvalidPolicySchema)
{
    const auto result = JsonTestPlanParser{}.ParseText(R"({
        "format":"ARTest.Script","version":1,"instruments":[],
        "commands":[{
            "stepId":1,"name":"Time.WaitMs","instrument":"NoInstrument","params":{"milliseconds":0},
            "policy":{"onFailure":"ignore"}
        }]
    })");

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_POLICY_SCHEMA_INVALID");
}

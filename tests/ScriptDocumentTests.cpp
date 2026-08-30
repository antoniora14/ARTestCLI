#include "TestSupport/TemporaryScript.h"

#include "ArtCore/ScriptDocument.h"

#include <gtest/gtest.h>

TEST(ScriptDocumentTests, LoadsTheVersionedCanonicalFormat)
{
    TemporaryScript script{R"({
        "format":"ARTest.Script",
        "version":1,
        "instruments":[],
        "commands":[]
    })"};

    const auto result = ScriptDocumentLoader::Load(script.Path());

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.value->instruments.empty());
    EXPECT_TRUE(result.value->commands.empty());
}

TEST(ScriptDocumentTests, RejectsTheLegacyArrayRoot)
{
    TemporaryScript script{R"([{"name":"Time.WaitMs"}])"};

    const auto result = ScriptDocumentLoader::Load(script.Path());

    ASSERT_FALSE(result.Succeeded());
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().code, "SCRIPT_ROOT_INVALID");
}

TEST(ScriptDocumentTests, RejectsUnsupportedVersions)
{
    TemporaryScript script{R"({
        "format":"ARTest.Script",
        "version":999,
        "instruments":[],
        "commands":[]
    })"};

    const auto result = ScriptDocumentLoader::Load(script.Path());

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "SCRIPT_VERSION_UNSUPPORTED");
}

TEST(ScriptDocumentTests, RejectsMalformedJson)
{
    TemporaryScript script{"{ incomplete"};

    const auto result = ScriptDocumentLoader::Load(script.Path());

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "SCRIPT_JSON_INVALID");
}

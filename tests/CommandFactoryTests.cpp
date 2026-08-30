#include "ArtCore/CCommandFactory.h"
#include "ArtCore/ScriptExecutor.h"

#include <gtest/gtest.h>

namespace
{
    nlohmann::json PowerSupplyDefinition()
    {
        return nlohmann::json::array({{{
            "type", "PowerSupply"
        }, {
            "id", "PS1"
        }, {
            "config", {{"hw-rsrc", "TEST::PS"}}
        }}});
    }

    nlohmann::json ValidWait(std::uint64_t stepId)
    {
        return {
            {"stepId", stepId},
            {"name", "Time.WaitMs"},
            {"instrument", "NoInstrument"},
            {"params", {{"milliseconds", 0}}}
        };
    }
}

TEST(CommandFactoryTests, UnknownCommandInvalidatesTheWholeScript)
{
    InstrumentFactory instruments;
    ASSERT_TRUE(instruments.LoadDefinitions(nlohmann::json::array()).Succeeded());
    const nlohmann::json commands = nlohmann::json::array({
        ValidWait(1),
        {{"stepId", 2}, {"name", "Unknown.Command"}, {"instrument", "NoInstrument"}, {"params", nlohmann::json::object()}}
    });

    const auto result = CommandFactory::CreateCommands(commands, instruments);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_FALSE(result.value.has_value());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_TYPE_UNKNOWN");
}

TEST(CommandFactoryTests, UnknownInstrumentInvalidatesTheWholeScript)
{
    InstrumentFactory instruments;
    ASSERT_TRUE(instruments.LoadDefinitions(nlohmann::json::array()).Succeeded());
    const nlohmann::json commands = nlohmann::json::array({{{
        "stepId", 1
    }, {
        "name", "PowerSupply.TurnOn"
    }, {
        "instrument", "missing"
    }, {
        "params", {{"channel", 1}, {"voltage", 12.0}, {"currentLimit", 2.0}}
    }}});

    const auto result = CommandFactory::CreateCommands(commands, instruments);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_INSTRUMENT_UNKNOWN");
}

TEST(CommandFactoryTests, DuplicateStepIdentifiersAreRejected)
{
    InstrumentFactory instruments;
    ASSERT_TRUE(instruments.LoadDefinitions(nlohmann::json::array()).Succeeded());
    const nlohmann::json commands = nlohmann::json::array({ValidWait(1), ValidWait(1)});

    const auto result = CommandFactory::CreateCommands(commands, instruments);

    ASSERT_FALSE(result.Succeeded());
    EXPECT_EQ(result.diagnostics.front().code, "COMMAND_STEP_ID_INVALID");
}

TEST(CommandFactoryTests, MissingParametersFailDuringOfflineCompilation)
{
    InstrumentFactory instruments;
    ASSERT_TRUE(instruments.LoadDefinitions(nlohmann::json::array()).Succeeded());
    const nlohmann::json commands = nlohmann::json::array({{{
        "stepId", 1
    }, {
        "name", "Time.WaitMs"
    }, {
        "instrument", "NoInstrument"
    }, {
        "params", nlohmann::json::object()
    }}});
    auto created = CommandFactory::CreateCommands(commands, instruments);
    ASSERT_TRUE(created.Succeeded());
    CScriptExecutor executor{std::move(*created.value)};

    const OperationResult compilation = executor.Compile();

    ASSERT_FALSE(compilation.Succeeded());
    EXPECT_EQ(compilation.diagnostics.front().code, "COMMAND_VALIDATION_FAILED");
}

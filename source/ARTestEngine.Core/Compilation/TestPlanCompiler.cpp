#include "TestPlanCompiler.h"

#include <exception>
#include <unordered_set>
#include <utility>

namespace
{
    void AppendDiagnostics(
        std::vector<artest::Diagnostic>& target,
        artest::OperationResult source,
        const std::string& location)
    {
        for (auto& diagnostic : source.diagnostics)
        {
            if (diagnostic.location.empty())
            {
                diagnostic.location = location;
            }
            target.push_back(std::move(diagnostic));
        }
    }
}

namespace artest
{
    TestPlanCompiler::TestPlanCompiler(CommandRegistry& commands, InstrumentManager& instruments)
        : m_commands(commands), m_instruments(instruments)
    {
    }

    ValueResult<std::vector<CompiledStep>> TestPlanCompiler::Compile(const TestPlan& plan) const
    {
        ValueResult<std::vector<CompiledStep>> result;
        std::vector<CompiledStep> compiledSteps;
        std::unordered_set<std::uint64_t> stepIds;

        if (plan.steps.empty())
        {
            result.diagnostics.push_back({
                DiagnosticSeverity::Error,
                "SCRIPT_EMPTY",
                "The test plan must contain at least one step.",
                "steps"});
            return result;
        }

        for (std::size_t index = 0; index < plan.steps.size(); ++index)
        {
            const auto& definition = plan.steps[index];
            const std::string location = "steps[" + std::to_string(index) + "]";

            if (definition.stepId == 0 || !stepIds.insert(definition.stepId).second)
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "COMMAND_STEP_ID_INVALID",
                    "Step identifiers must be unique positive integers.",
                    location});
                continue;
            }

            std::shared_ptr<IInstrument> instrument;
            if (definition.instrumentId.has_value())
            {
                instrument = m_instruments.GetInstrument(*definition.instrumentId);
                if (!instrument)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error,
                        "COMMAND_INSTRUMENT_UNKNOWN",
                        "Unknown instrument ID: " + *definition.instrumentId,
                        location});
                    continue;
                }
            }

            try
            {
                auto command = m_commands.Create(definition.commandName);
                if (!command)
                {
                    result.diagnostics.push_back({
                        DiagnosticSeverity::Error,
                        "COMMAND_TYPE_UNKNOWN",
                        "Unknown command type: " + definition.commandName,
                        location});
                    continue;
                }

                OperationResult configuration = command->Configure(
                    definition.parameters,
                    std::move(instrument));
                if (!configuration.Succeeded())
                {
                    AppendDiagnostics(result.diagnostics, std::move(configuration), location);
                    continue;
                }

                OperationResult validation = command->Validate();
                if (!validation.Succeeded())
                {
                    AppendDiagnostics(
                        result.diagnostics,
                        std::move(validation),
                        "stepId=" + std::to_string(definition.stepId));
                    continue;
                }

                compiledSteps.push_back({
                    definition.stepId,
                    definition.commandName,
                    std::move(command)});
            }
            catch (const std::exception& exception)
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "COMMAND_COMPILATION_EXCEPTION",
                    exception.what(),
                    location});
            }
            catch (...)
            {
                result.diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "COMMAND_COMPILATION_EXCEPTION",
                    "Unknown exception while compiling the command.",
                    location});
            }
        }

        if (!ContainsErrors(result.diagnostics))
        {
            result.value = std::move(compiledSteps);
        }
        return result;
    }
}

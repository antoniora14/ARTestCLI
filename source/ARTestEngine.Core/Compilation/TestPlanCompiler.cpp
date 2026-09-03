#include "TestPlanCompiler.h"
#include "../Catalog/SchemaValidator.h"
#include <algorithm>
#include <map>
#include <unordered_set>

namespace artest
{
    ValueResult<std::vector<CompiledStep>> TestPlanCompiler::Compile(const TestPlan& plan) const
    {
        ValueResult<std::vector<CompiledStep>> result;
        std::vector<CompiledStep> steps;
        std::map<std::string, const ComponentDescriptor*> instruments;
        const auto fail = [&result](std::string code, std::string message, std::string location)
        { result.diagnostics.push_back({DiagnosticSeverity::Error, std::move(code), std::move(message), std::move(location)}); };
        const auto append = [&result](const OperationResult& operation)
        { result.diagnostics.insert(result.diagnostics.end(), operation.diagnostics.begin(), operation.diagnostics.end()); };

        for (const auto& instrument : plan.instruments)
        {
            if (instrument.id.empty() || instruments.contains(instrument.id))
            {
                fail("INSTRUMENT_ID_INVALID", "Instrument IDs must be non-empty and unique.", instrument.id);
                continue;
            }
            const auto* descriptor = m_catalog.Find(instrument.type);
            if (!descriptor || descriptor->kind != ComponentKind::InstrumentDriver)
            {
                fail("INSTRUMENT_TYPE_UNKNOWN", "Unknown instrument type: " + instrument.type, instrument.id);
                continue;
            }
            instruments.emplace(instrument.id, descriptor);
            if (const auto* schema = descriptor->Schema("configuration"))
                append(SchemaValidator::Validate(schema->document, instrument.configuration,
                    "instruments/" + instrument.id + "/config"));
            else
                fail("INSTRUMENT_SCHEMA_MISSING", "A declarative configuration schema is required.", instrument.id);
        }
        if (plan.steps.empty()) fail("SCRIPT_EMPTY", "The test plan must contain at least one step.", "steps");
        std::unordered_set<std::uint64_t> ids;
        for (std::size_t index = 0; index < plan.steps.size(); ++index)
        {
            const auto& step = plan.steps[index];
            const auto location = "steps[" + std::to_string(index) + "]";
            if (step.stepId == 0 || !ids.insert(step.stepId).second)
            { fail("COMMAND_STEP_ID_INVALID", "Step identifiers must be unique positive integers.", location); continue; }
            if (step.policy.maxAttempts < 1 || step.policy.maxAttempts > 100)
            { fail("COMMAND_POLICY_ATTEMPTS_INVALID", "maxAttempts must be between 1 and 100.", location); continue; }
            if (step.policy.retryDelay.count() < 0)
            { fail("COMMAND_POLICY_RETRY_DELAY_INVALID", "retryDelayMs must be zero or greater.", location); continue; }
            if (step.policy.timeout.count() < 0)
            { fail("COMMAND_POLICY_TIMEOUT_INVALID", "timeoutMs must be zero or greater.", location); continue; }
            const ComponentDescriptor* instrument = nullptr;
            if (step.instrumentId)
            {
                const auto found = instruments.find(*step.instrumentId);
                if (found == instruments.end())
                { fail("COMMAND_INSTRUMENT_UNKNOWN", "Unknown instrument ID: " + *step.instrumentId, location); continue; }
                instrument = found->second;
            }
            const auto* descriptor = m_catalog.Find(step.commandName);
            if (!descriptor || descriptor->kind != ComponentKind::Command)
            { fail("COMMAND_TYPE_UNKNOWN", "Unknown command type: " + step.commandName, location); continue; }
            if (!descriptor->unavailableCode.empty())
            { fail(descriptor->unavailableCode, "This intrinsic is reserved and is not implemented.", location); continue; }
            bool bindingValid = true;
            for (const auto& contract : descriptor->requiredContracts)
                // ABI 0.1 exposes one service contract per driver component.
                // Capability tags are discovery metadata, not additional service interfaces.
                if (!instrument || instrument->contractId != contract)
                {
                    fail("COMMAND_INSTRUMENT_CONTRACT_MISMATCH", "A configured instrument must provide " + contract + ".", location);
                    bindingValid = false;
                }
            if (!bindingValid) continue;
            const auto* schema = descriptor->Schema("parameters");
            if (!schema)
            { fail("COMMAND_SCHEMA_MISSING", "A declarative parameter schema is required.", location); continue; }
            auto validation = SchemaValidator::Validate(schema->document, step.parameters, location + "/params");
            if (!validation.Succeeded())
            {
                if (!descriptor->validationCode.empty())
                    for (auto& diagnostic : validation.diagnostics)
                    {
                        diagnostic.code = descriptor->validationCode;
                        diagnostic.location = "stepId=" + std::to_string(step.stepId);
                    }
                append(validation);
                continue;
            }
            steps.push_back({step.stepId, step.commandName, descriptor->typeId,
                step.parameters, step.instrumentId, step.policy});
        }
        if (!ContainsErrors(result.diagnostics)) result.value = std::move(steps);
        return result;
    }
}

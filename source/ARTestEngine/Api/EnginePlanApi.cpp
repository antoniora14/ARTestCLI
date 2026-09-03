#include "../../ARTestEngine.Core/Compilation/TestPlanCompiler.h"
#include "../../ARTestEngine.Core/Parsing/JsonTestPlanParser.h"
#include "EngineFunctions.h"
#include "EngineHandles.h"
#include "EngineMarshalling.h"
namespace artest::engine
{
struct CompileAttempt
{
    std::unique_ptr<ARTestCompiledPlanOpaque> plan;
    std::vector<artest::Diagnostic> diagnostics;
    std::size_t instrumentCount = 0U;
    std::size_t stepCount = 0U;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return plan != nullptr && !artest::ContainsErrors(diagnostics);
    }
};

void AppendDiagnostics(std::vector<artest::Diagnostic> &destination,
                       const std::vector<artest::Diagnostic> &source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

[[nodiscard]] CompileAttempt CompileForEngine(EngineContext &engine,
                                              const ARTestPayloadView *payload)
{
    CompileAttempt attempt;
    std::scoped_lock lock{engine.mutex};
    artest::JsonTestPlanParser parser;
    auto parsed = parser.ParseText(PayloadText(payload), "engine-api");
    AppendDiagnostics(attempt.diagnostics, parsed.diagnostics);
    if (!parsed.Succeeded())
        return attempt;

    attempt.instrumentCount = parsed.value->instruments.size();
    attempt.stepCount = parsed.value->steps.size();
    artest::TestPlanCompiler compiler{engine.catalog};
    auto compiled = compiler.Compile(*parsed.value);
    AppendDiagnostics(attempt.diagnostics, compiled.diagnostics);
    if (!compiled.Succeeded())
        return attempt;

    attempt.plan = std::make_unique<ARTestCompiledPlanOpaque>();
    attempt.plan->owner = &engine;
    attempt.plan->plan = std::move(*parsed.value);
    for (auto &definition : attempt.plan->plan.instruments)
        definition.type = engine.catalog.Find(definition.type)->typeId;
    attempt.plan->steps = std::move(*compiled.value);
    attempt.plan->revision = engine.revision;
    return attempt;
}

ARTestStatus ARTEST_ABI_CALL CompilePlan(ARTestEngineHandle engine,
                                         const ARTestPayloadView *payload,
                                         ARTestCompiledPlanHandle *output, ARTestErrorBuffer *error)
{
    if (engine == nullptr || output == nullptr)
    {
        SetError(error, "Engine and compiled-plan output handles are required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        *output = nullptr;
        auto attempt = CompileForEngine(*engine->value, payload);
        if (!attempt.Succeeded())
        {
            SetError(error, DiagnosticsText(attempt.diagnostics));
            return ARTEST_STATUS_INVALID_ARGUMENT;
        }
        *output = attempt.plan.release();
        return ARTEST_STATUS_OK;
    }
    catch (const std::invalid_argument &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    catch (const std::exception &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
    catch (...)
    {
        SetError(error, "Unknown failure while compiling the plan.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

ARTestStatus ARTEST_ABI_CALL CompilePlanDetailed(ARTestEngineHandle engine,
                                                 const ARTestPayloadView *payload,
                                                 ARTestCompiledPlanHandle *output,
                                                 const ARTestResultSinkV0 *reportSink,
                                                 ARTestErrorBuffer *error)
{
    if (engine == nullptr || output == nullptr)
    {
        SetError(error, "Engine and compiled-plan output handles are required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        *output = nullptr;
        auto attempt = CompileForEngine(*engine->value, payload);
        const auto report = SerializeCompileResult(attempt.Succeeded(), attempt.instrumentCount,
                                                   attempt.stepCount, attempt.diagnostics);
        const auto writeStatus = WriteJson(report, reportSink, error);
        if (writeStatus != ARTEST_STATUS_OK)
            return writeStatus;
        if (attempt.Succeeded())
            *output = attempt.plan.release();
        return ARTEST_STATUS_OK;
    }
    catch (const std::invalid_argument &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    catch (const std::exception &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
    catch (...)
    {
        SetError(error, "Unknown failure while compiling the plan.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

void ARTEST_ABI_CALL DestroyCompiledPlan(ARTestCompiledPlanHandle plan)
{
    delete plan;
}

} // namespace artest::engine

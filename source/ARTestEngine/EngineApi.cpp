#include "Api/EngineFunctions.h"
#include "Api/EngineMarshalling.h"
#include <algorithm>
#include <cstring>

using namespace artest::engine;

extern "C" ARTEST_ENGINE_EXPORT ARTestStatus ARTEST_ABI_CALL
    ARTestEngine_QueryApi(
        std::uint32_t requestedMajor,
        std::uint32_t requestedMinor,
        ARTestEngineApiV0* api,
        ARTestErrorBuffer* error)
{
    if (api == nullptr)
    {
        SetError(error, "An ARTestEngineApiV0 output table is required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    if (requestedMajor != ARTEST_ENGINE_API_MAJOR
        || requestedMinor > ARTEST_ENGINE_API_MINOR)
    {
        SetError(error, "The requested ARTestEngine API version is incompatible.");
        return ARTEST_STATUS_INCOMPATIBLE_ABI;
    }
    const auto negotiatedSize = requestedMinor >= 4U
        ? static_cast<std::uint32_t>(sizeof(ARTestEngineApiV0))
        : requestedMinor >= 3U ? ARTEST_ENGINE_API_V0_3_SIZE
        : requestedMinor >= 2U
            ? ARTEST_ENGINE_API_V0_2_SIZE
            : ARTEST_ENGINE_API_V0_1_SIZE;
    if (api->struct_size < negotiatedSize)
    {
        SetError(error, "The Engine API output table is smaller than the requested minor version.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }

    ARTestEngineApiV0 table{
        negotiatedSize,
        ARTEST_ENGINE_API_MAJOR,
        requestedMinor,
        0U,
        &CreateEngine,
        &DestroyEngine,
        &RefreshCatalog,
        &GetCatalogSnapshot,
        &CompilePlan,
        &DestroyCompiledPlan,
        &SubscribeEvents,
        &UnsubscribeEvents,
        &StartSession,
        &CancelSession,
        &WaitSession,
        &GetSessionState,
        &GetSessionResult,
        &DestroySession,
        &SerializeRunResult,
        &DestroyRunResult,
        &CompilePlanDetailed,
        &StartSessionControlled,
        &ValidateCatalog,
        &PrepareCatalog};
    // Even a larger caller buffer belongs to the host beyond the negotiated prefix.
    std::memcpy(api, &table, negotiatedSize);
    return ARTEST_STATUS_OK;
}

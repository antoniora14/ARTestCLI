#include "EngineFunctions.h"
#include "EngineHandles.h"
#include "EngineMarshalling.h"
namespace artest::engine
{
ARTestStatus ARTEST_ABI_CALL RefreshCatalog(ARTestEngineHandle engine,
                                            ARTestStringView approvedRoot, ARTestErrorBuffer *error)
{
    if (engine == nullptr)
    {
        SetError(error, "A valid engine handle is required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        auto result = engine->value->Prepare(std::filesystem::path{ToString(approvedRoot)});
        if (result.Succeeded())
            result = engine->value->Activate();
        if (!result.Succeeded())
        {
            SetError(error, DiagnosticsText(result.diagnostics));
            return ARTEST_STATUS_EXTENSION_FAILURE;
        }
        return ARTEST_STATUS_OK;
    }
    catch (const std::exception &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
    catch (...)
    {
        SetError(error, "Unknown failure while refreshing the extension catalog.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

ARTestStatus ARTEST_ABI_CALL ValidateCatalog(ARTestEngineHandle engine,
                                             ARTestStringView approvedRoot,
                                             const ARTestResultSinkV0 *sink,
                                             ARTestErrorBuffer *error)
{
    if (engine == nullptr)
    {
        SetError(error, "A valid engine handle is required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        return WriteJson(
            engine->value->runtime->ValidateCatalog(std::filesystem::path{ToString(approvedRoot)}),
            sink, error);
    }
    catch (const std::exception &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
    catch (...)
    {
        SetError(error, "Unknown failure while validating the extension catalog.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

ARTestStatus ARTEST_ABI_CALL PrepareCatalog(ARTestEngineHandle engine, ARTestStringView root,
                                            ARTestErrorBuffer *error)
{
    if (!engine)
        return ARTEST_STATUS_INVALID_ARGUMENT;
    try
    {
        auto result = engine->value->Prepare(std::filesystem::path{ToString(root)});
        if (result.Succeeded())
            return ARTEST_STATUS_OK;
        SetError(error, DiagnosticsText(result.diagnostics));
        return ARTEST_STATUS_EXTENSION_FAILURE;
    }
    catch (const std::exception &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
    catch (...)
    {
        SetError(error, "Catalog preparation failed.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

ARTestStatus ARTEST_ABI_CALL GetCatalogSnapshot(ARTestEngineHandle engine,
                                                const ARTestResultSinkV0 *sink,
                                                ARTestErrorBuffer *error)
{
    if (engine == nullptr)
    {
        SetError(error, "A valid engine handle is required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        nlohmann::json snapshot;
        {
            std::scoped_lock lock{engine->value->mutex};
            snapshot = engine->value->lastCatalogReport;
        }
        if (snapshot.is_null())
            snapshot = engine->value->runtime->CatalogSnapshot();
        return WriteJson(snapshot, sink, error);
    }
    catch (const std::exception &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
    catch (...)
    {
        SetError(error, "Unknown failure while serializing the catalog.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

} // namespace artest::engine

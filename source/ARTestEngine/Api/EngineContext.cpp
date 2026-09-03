#include "../../ARTestEngine.Core/Commands/BuiltIn/IntrinsicCommands.h"
#include "EngineFunctions.h"
#include "EngineHandles.h"
#include "EngineMarshalling.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
namespace artest::engine
{
EngineContext::EngineContext()
    : runtime(std::make_shared<artest::extensions::NativeExtensionRuntime>(events))
{
}
artest::OperationResult EngineContext::Initialize(bool discoverDefault)
{
    auto result = artest::RegisterIntrinsicCommands(commands);
    if (!result.Succeeded())
        return result;
    result = artest::RegisterIntrinsicMetadata(catalog);
    if (!result.Succeeded())
        return result;
    if (!discoverDefault)
        return artest::OperationResult::Success();
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ARTestEngine_QueryApi), &module))
    {
        wchar_t path[32768]{};
        const auto length = GetModuleFileNameW(module, path, 32768);
        if (length > 0 && length < 32768)
        {
            const auto directory = std::filesystem::path{path}.parent_path();
            const auto root = directory.parent_path().parent_path().parent_path() / "extensions" /
                              directory.parent_path().filename() / directory.filename();
            if (std::filesystem::is_directory(root))
                return Prepare(root);
        }
    }
    return artest::OperationResult::Success();
}
artest::OperationResult EngineContext::Prepare(const std::filesystem::path &root)
{
    std::scoped_lock lock{mutex};
    if (active || loading || !runLease.expired())
        return artest::OperationResult::Failure(
            "EXTENSION_CATALOG_LOCKED",
            "Catalog preparation requires an inactive Engine without session handles.");
    auto scan = artest::extensions::ExtensionCatalog{}.Discover(root);
    artest::OperationResult result{scan.diagnostics};
    for (const auto &package : scan.packages)
        result.diagnostics.insert(result.diagnostics.end(), package.diagnostics.begin(),
                                  package.diagnostics.end());
    lastCatalogReport =
        scan.ToJson(scan.IsValid() ? "validated" : "rejected", 0, nlohmann::json::array());
    if (!scan.IsValid())
        return result;
    artest::ComponentCatalog next;
    result = artest::RegisterIntrinsicMetadata(next);
    if (!result.Succeeded())
        return result;
    result = next.Add(scan.Components());
    if (!result.Succeeded())
    {
        scan.diagnostics.insert(scan.diagnostics.end(), result.diagnostics.begin(),
                                result.diagnostics.end());
        lastCatalogReport = scan.ToJson("rejected", 0, nlohmann::json::array());
        return result;
    }
    catalog = std::move(next);
    prepared = std::move(scan);
    ++revision;
    return artest::OperationResult::Success();
}
artest::OperationResult EngineContext::Activate()
{
    std::filesystem::path root;
    std::string fingerprint;
    {
        std::scoped_lock lock{mutex};
        if (active || prepared.packages.empty())
            return artest::OperationResult::Success();
        if (loading)
            return artest::OperationResult::Failure("EXTENSION_CATALOG_BUSY",
                                                    "Activation is already in progress.");
        root = prepared.root;
        fingerprint = prepared.Fingerprint();
        loading = true;
    }
    artest::OperationResult result;
    nlohmann::json report;
    try
    {
        result = runtime->Refresh(root, commands, instruments, fingerprint);
        report = runtime->CatalogSnapshot();
    }
    catch (...)
    {
        std::scoped_lock lock{mutex};
        loading = false;
        throw;
    }
    {
        std::scoped_lock lock{mutex};
        loading = false;
        active = result.Succeeded();
        lastCatalogReport = std::move(report);
    }
    return result;
}
ARTestStatus ARTEST_ABI_CALL CreateEngine(const ARTestPayloadView *configuration,
                                          ARTestEngineHandle *output, ARTestErrorBuffer *error)
{
    if (output == nullptr)
    {
        SetError(error, "An engine output pointer is required.");
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        *output = nullptr;
        const auto options = configuration == nullptr
                                 ? nlohmann::json::object()
                                 : nlohmann::json::parse(PayloadText(configuration));
        if (!options.is_object())
            throw std::invalid_argument("Engine configuration must be an object.");
        auto engine = std::make_unique<ARTestEngineOpaque>();
        engine->value = std::make_unique<EngineContext>();
        const auto initialized =
            engine->value->Initialize(options.value("loadDefaultCatalog", true));
        if (!initialized.Succeeded())
        {
            SetError(error, DiagnosticsText(initialized.diagnostics));
            return ARTEST_STATUS_INTERNAL_FAILURE;
        }
        *output = engine.release();
        return ARTEST_STATUS_OK;
    }
    catch (const std::exception &exception)
    {
        SetError(error, exception.what());
        return ARTEST_STATUS_INVALID_ARGUMENT;
    }
    catch (...)
    {
        SetError(error, "Unknown failure while creating ARTestEngine.");
        return ARTEST_STATUS_INTERNAL_FAILURE;
    }
}

void ARTEST_ABI_CALL DestroyEngine(ARTestEngineHandle engine)
{
    delete engine;
}

} // namespace artest::engine

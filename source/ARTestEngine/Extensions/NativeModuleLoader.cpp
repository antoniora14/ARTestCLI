#include "NativeModuleLoader.h"
namespace artest::extensions
{
LoadedCatalog LoadNativeModules(CatalogScan &scan, const ARTestHostApiV0 &hostApi)
{
    LoadedCatalog candidate;
    auto &loaded = candidate.modules;
    auto &types = candidate.types;
    const auto addFailure = [](CatalogPackage &package, std::string code, std::string message,
                               const std::filesystem::path &location) {
        package.diagnostics.push_back(
            {DiagnosticSeverity::Error, std::move(code), std::move(message), location.string()});
    };

    for (auto &package : scan.packages)
    {
        auto module = std::make_shared<NativeModule>();
        module->packageRoot = package.packageRoot;
        module->manifest = package.manifest;
        module->manifestText = package.manifestText;
        module->extensionId = package.extensionId;
        module->library =
            LoadLibraryExW(package.entryPath.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (module->library == nullptr)
        {
            addFailure(package, "EXTENSION_LOAD_FAILED",
                       "LoadLibrary failed for the extension entry.", package.entryPath);
            continue;
        }

        const auto query = reinterpret_cast<ARTestExtensionQueryFn>(
            GetProcAddress(module->library, "ARTestExtension_Query"));
        if (query == nullptr)
        {
            addFailure(package, "EXTENSION_QUERY_MISSING", "The extension query export is missing.",
                       package.entryPath);
            continue;
        }

        module->api.struct_size = sizeof(ARTestExtensionApiV0);
        ErrorStorage queryError;
        auto status = query(ARTEST_EXTENSION_ABI_MAJOR, ARTEST_EXTENSION_ABI_MINOR, &module->api,
                            &queryError.buffer);
        if (status != ARTEST_STATUS_OK || module->api.struct_size < sizeof(ARTestExtensionApiV0) ||
            module->api.abi_major != ARTEST_EXTENSION_ABI_MAJOR ||
            module->api.abi_minor > ARTEST_EXTENSION_ABI_MINOR ||
            module->api.create_extension == nullptr || module->api.destroy_extension == nullptr ||
            module->api.get_component_type_count == nullptr ||
            module->api.get_component_descriptor == nullptr ||
            module->api.create_component == nullptr || module->api.destroy_component == nullptr ||
            module->api.invoke_component == nullptr)
        {
            addFailure(package, "EXTENSION_ABI_INVALID",
                       queryError.Message("The extension function table is invalid."),
                       package.entryPath);
            continue;
        }
        if (ToString(module->api.extension_id) != module->extensionId)
        {
            addFailure(package, "EXTENSION_ID_MISMATCH",
                       "The manifest and binary extension IDs differ.", package.manifestPath);
            continue;
        }
        if (ToString(module->api.extension_version) != package.descriptor.version)
        {
            addFailure(package, "EXTENSION_VERSION_MISMATCH",
                       "The manifest and binary extension versions differ.", package.manifestPath);
            continue;
        }

        const auto manifestPayload = JsonPayload(module->manifestText);
        ErrorStorage createError;
        status = module->api.create_extension(&hostApi, &manifestPayload, &module->extension,
                                              &createError.buffer);
        if (status != ARTEST_STATUS_OK || module->extension == nullptr)
        {
            addFailure(package, "EXTENSION_CREATE_FAILED",
                       createError.Message("The extension could not be created."),
                       package.entryPath);
            continue;
        }

        const auto count = module->api.get_component_type_count(module->extension);
        if (count != package.descriptor.components.size())
        {
            addFailure(package, "EXTENSION_COMPONENT_COUNT_MISMATCH",
                       "The manifest and binary component counts differ.", package.manifestPath);
            continue;
        }

        bool descriptorFailure = false;
        for (std::size_t index = 0; index < count; ++index)
        {
            ARTestComponentDescriptorV0 descriptor{};
            descriptor.struct_size = sizeof(descriptor);
            ErrorStorage descriptorError;
            status = module->api.get_component_descriptor(module->extension, index, &descriptor,
                                                          &descriptorError.buffer);
            const auto &declared = package.descriptor.components[index];
            ComponentRecord record{declared.kind == ComponentKind::Command
                                       ? ARTEST_COMPONENT_KIND_COMMAND
                                   : declared.kind == ComponentKind::InstrumentDriver
                                       ? ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER
                                       : ARTEST_COMPONENT_KIND_TOOL,
                                   ARTEST_COMPONENT_FLAG_NONE,
                                   declared.typeId,
                                   declared.contractId,
                                   declared.version,
                                   declared.displayName};
            {
                for (const auto &flag : declared.flags)
                {
                    if (flag == "simulated")
                        record.flags |= ARTEST_COMPONENT_FLAG_SIMULATED;
                    if (flag == "requiresHardware")
                        record.flags |= ARTEST_COMPONENT_FLAG_REQUIRES_HARDWARE;
                }
            }
            if (status != ARTEST_STATUS_OK ||
                descriptor.struct_size < sizeof(ARTestComponentDescriptorV0) ||
                descriptor.kind != record.kind || descriptor.flags != record.flags ||
                ToString(descriptor.component_version) != record.version ||
                ToString(descriptor.type_id) != record.typeId ||
                ToString(descriptor.contract_id) != record.contractId)
            {
                addFailure(package, "EXTENSION_DESCRIPTOR_MISMATCH",
                           descriptorError.Message("The manifest and binary descriptor differ."),
                           package.manifestPath);
                descriptorFailure = true;
                break;
            }
            const auto typeKey = record.typeId;
            module->components.push_back(record);
            types.emplace(typeKey, std::make_pair(module, std::move(record)));
        }
        if (!descriptorFailure)
            loaded.push_back(std::move(module));
    }
    return candidate;
}

} // namespace artest::extensions

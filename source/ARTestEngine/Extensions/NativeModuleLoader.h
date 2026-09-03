#pragma once
#include "ExtensionCatalog.h"
#include "NativeModule.h"
namespace artest::extensions
{
struct LoadedCatalog
{
    std::vector<std::shared_ptr<NativeModule>> modules;
    NativeTypeMap types;
};
LoadedCatalog LoadNativeModules(CatalogScan &scan, const ARTestHostApiV0 &hostApi);

} // namespace artest::extensions

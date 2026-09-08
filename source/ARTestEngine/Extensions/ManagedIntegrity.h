#pragma once
#include "ExtensionCatalog.h"
#include "../../ARTestEngine.Process/ManagedPackage.h"
namespace artest::extensions
{
process::ManagedPackageRequirements ValidateManagedPackage(const CatalogPackage &package);
// A receipt is machine-local deployment configuration, never package-authored launch code.
struct PythonEnvironment { std::filesystem::path interpreter, launcher; };
PythonEnvironment ValidatePythonEnvironment(const CatalogPackage &package,
                                           const std::filesystem::path &receipt);
}

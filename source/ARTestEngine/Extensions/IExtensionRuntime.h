#pragma once
#include "../../ARTestEngine.Core/Commands/CommandRegistry.h"
#include "../../ARTestEngine.Core/Instruments/InstrumentRegistry.h"
#include "../../ARTestEngine.Core/Execution/Cancellation.h"
#include <filesystem>
#include <memory>

namespace artest::extensions
{
// Internal ownership boundary. No native ABI handle or worker token reaches Core.
class ComponentLease
{
  public:
    virtual ~ComponentLease() = default;
};

class IExtensionRuntime
{
  public:
    virtual ~IExtensionRuntime() = default;
    virtual nlohmann::json ValidateCatalog(const std::filesystem::path &root) const = 0;
    virtual OperationResult Refresh(const std::filesystem::path &root, CommandRegistry &commands,
                                    InstrumentRegistry &instruments, const std::string &fingerprint = {}) = 0;
    virtual nlohmann::json CatalogSnapshot() const = 0;
    virtual ValueResult<std::shared_ptr<ComponentLease>> CreateComponent(
        const std::string &typeId, const nlohmann::json &configuration) = 0;
    virtual OperationResult Invoke(const std::shared_ptr<ComponentLease> &component,
        const std::string &operation, const nlohmann::json &request,
        const CancellationToken *cancellation, nlohmann::json *response) = 0;
    virtual OperationResult RegisterService(std::string instanceId,
                                            const std::shared_ptr<ComponentLease> &component) = 0;
    virtual void UnregisterService(const std::string &instanceId) noexcept = 0;
};
} // namespace artest::extensions

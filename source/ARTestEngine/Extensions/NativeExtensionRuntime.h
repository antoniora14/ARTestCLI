#pragma once

#include "../../ARTestEngine.Core/Commands/CommandRegistry.h"
#include "../../ARTestEngine.Core/Diagnostics.h"
#include "../../ARTestEngine.Core/Execution/Cancellation.h"
#include "../../ARTestEngine.Core/Execution/IEventSink.h"
#include "../../ARTestEngine.Core/Instruments/InstrumentRegistry.h"
#include "../../ThirdParty/json.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace artest::extensions
{
class NativeComponentInstance;
class NativeExtensionRuntime final : public std::enable_shared_from_this<NativeExtensionRuntime>
{
  public:
    explicit NativeExtensionRuntime(IEventSink &eventSink);
    ~NativeExtensionRuntime();
    NativeExtensionRuntime(const NativeExtensionRuntime &) = delete;
    NativeExtensionRuntime &operator=(const NativeExtensionRuntime &) = delete;

    [[nodiscard]] nlohmann::json ValidateCatalog(const std::filesystem::path &approvedRoot) const;
    [[nodiscard]] OperationResult Refresh(const std::filesystem::path &approvedRoot,
                                          CommandRegistry &commands,
                                          InstrumentRegistry &instruments,
                                          const std::string &expectedFingerprint = {});
    [[nodiscard]] nlohmann::json CatalogSnapshot() const;
    [[nodiscard]] ValueResult<std::shared_ptr<NativeComponentInstance>> CreateComponent(
        const std::string &typeId, const nlohmann::json &configuration);
    [[nodiscard]] OperationResult Invoke(const std::shared_ptr<NativeComponentInstance> &component,
                                         const std::string &operationId,
                                         const nlohmann::json &request,
                                         const CancellationToken *cancellation,
                                         nlohmann::json *response);
    [[nodiscard]] OperationResult RegisterService(
        std::string instanceId, const std::shared_ptr<NativeComponentInstance> &component);
    void UnregisterService(const std::string &instanceId) noexcept;

  private:
    class Implementation;
    std::unique_ptr<Implementation> m_implementation;
};
} // namespace artest::extensions

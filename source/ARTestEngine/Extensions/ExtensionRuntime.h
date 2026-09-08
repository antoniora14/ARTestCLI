#pragma once
#include "IExtensionRuntime.h"
#include "../../ARTest.SDK/include/ARTestExtensionAbi.h"

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
class ExtensionRuntime final : public IExtensionRuntime,
                                     public std::enable_shared_from_this<ExtensionRuntime>
{
  public:
    explicit ExtensionRuntime(IEventSink &eventSink);
    ~ExtensionRuntime() override;
    void Configure(const nlohmann::json &options) override;
    OperationResult BeginSession() override;
    OperationResult EndSession() override;
    ARTestStatus InvokeAbi(const std::shared_ptr<ComponentLease>&, ARTestStringView,
        const ARTestPayloadView*, const ARTestInvocationContextV0*, const ARTestResultSinkV0*, ARTestErrorBuffer*);
    ExtensionRuntime(const ExtensionRuntime &) = delete;
    ExtensionRuntime &operator=(const ExtensionRuntime &) = delete;

    [[nodiscard]] nlohmann::json ValidateCatalog(const std::filesystem::path &approvedRoot) const override;
    [[nodiscard]] OperationResult Refresh(const std::filesystem::path &approvedRoot, CommandRegistry &commands, InstrumentRegistry &instruments, const std::string &expectedFingerprint = {}) override;
    [[nodiscard]] nlohmann::json CatalogSnapshot() const override;
    [[nodiscard]] ValueResult<std::shared_ptr<ComponentLease>> CreateComponent(const std::string &typeId, const nlohmann::json &configuration) override;
    [[nodiscard]] OperationResult Invoke(const std::shared_ptr<ComponentLease> &component,
                                         const std::string &operationId,
                                         const nlohmann::json &request,
                                         const CancellationToken *cancellation,
                                         InvocationOutput *response) override;
    [[nodiscard]] OperationResult RegisterService(std::string instanceId, const std::shared_ptr<ComponentLease> &component) override;
    void UnregisterService(const std::string &instanceId) noexcept override;

  private:
    class Implementation;
    std::unique_ptr<Implementation> m_implementation;
};
} // namespace artest::extensions

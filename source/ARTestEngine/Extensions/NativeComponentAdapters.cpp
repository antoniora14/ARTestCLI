#include "NativeComponentAdapters.h"
#include "NativeModule.h"
namespace artest::extensions
{
class NativeInstrumentAdapter final : public IInstrument
{
  public:
    NativeInstrumentAdapter(std::shared_ptr<NativeExtensionRuntime> runtime,
                            std::string typeId) noexcept
        : m_runtime(std::move(runtime)), m_typeId(std::move(typeId))
    {
    }
    [[nodiscard]] std::string GetId() const override
    {
        return m_id;
    }
    void SetId(std::string id) override
    {
        m_id = std::move(id);
    }
    [[nodiscard]] OperationResult Initialize(const nlohmann::json &configuration) override
    {
        auto created = m_runtime->CreateComponent(m_typeId, configuration);
        if (!created.Succeeded())
            return {std::move(created.diagnostics)};
        m_component = std::move(*created.value);
        auto initialized = m_runtime->Invoke(m_component, "artest.lifecycle.initialize.v1",
                                             nlohmann::json::object(), nullptr, nullptr);
        if (!initialized.Succeeded())
        {
            // Initialization may have acquired resources before it failed.
            // Shutdown is attempted even when the driver never became a service.
            const auto cleanup = m_runtime->Invoke(m_component, "artest.lifecycle.shutdown.v1",
                                                   nlohmann::json::object(), nullptr, nullptr);
            initialized.diagnostics.insert(initialized.diagnostics.end(),
                                           cleanup.diagnostics.begin(), cleanup.diagnostics.end());
            m_component.reset();
            return initialized;
        }
        auto registered = m_runtime->RegisterService(m_id, m_component);
        if (!registered.Succeeded())
        {
            const auto cleanup = m_runtime->Invoke(m_component, "artest.lifecycle.shutdown.v1",
                                                   nlohmann::json::object(), nullptr, nullptr);
            registered.diagnostics.insert(registered.diagnostics.end(), cleanup.diagnostics.begin(),
                                          cleanup.diagnostics.end());
            m_component.reset();
        }
        return registered;
    }
    [[nodiscard]] OperationResult Shutdown() override
    {
        m_runtime->UnregisterService(m_id);
        if (!m_component)
            return OperationResult::Success();
        auto result = m_runtime->Invoke(m_component, "artest.lifecycle.shutdown.v1",
                                        nlohmann::json::object(), nullptr, nullptr);
        m_component.reset();
        return result;
    }

  private:
    std::shared_ptr<NativeExtensionRuntime> m_runtime;
    std::string m_typeId;
    std::string m_id;
    std::shared_ptr<NativeComponentInstance> m_component;
};

class NativeCommandAdapter final : public ICommand
{
  public:
    NativeCommandAdapter(std::shared_ptr<NativeExtensionRuntime> runtime,
                         std::string typeId) noexcept
        : m_runtime(std::move(runtime)), m_typeId(std::move(typeId))
    {
    }
    [[nodiscard]] std::string Name() const override
    {
        return m_typeId;
    }
    [[nodiscard]] OperationResult Configure(const nlohmann::json &parameters,
                                            std::shared_ptr<IInstrument> instrument) override
    {
        m_request = {{"parameters", parameters},
                     {"instrumentId", instrument ? instrument->GetId() : std::string{}}};
        auto created = m_runtime->CreateComponent(m_typeId, parameters);
        if (!created.Succeeded())
            return {std::move(created.diagnostics)};
        m_component = std::move(*created.value);
        return OperationResult::Success();
    }
    [[nodiscard]] OperationResult Validate() const override
    {
        if (!m_component)
            return OperationResult::Failure("EXTENSION_COMMAND_NOT_CONFIGURED",
                                            "The extension command is not configured.");
        return m_runtime->Invoke(m_component, "artest.component.validate.v1", m_request, nullptr,
                                 nullptr);
    }
    [[nodiscard]] StepResult Execute(ExecutionContext &,
                                     const CancellationToken &cancellation) override
    {
        nlohmann::json response;
        const auto result = m_runtime->Invoke(m_component, "artest.command.execute.v1", m_request,
                                              &cancellation, &response);
        if (result.Succeeded())
            return StepResult::Pass(
                response.value("message", std::string{"Extension command passed."}));
        const auto message = result.diagnostics.empty() ? std::string{"Extension command failed."}
                                                        : result.diagnostics.front().message;
        if (cancellation.IsTimedOut())
            return StepResult::Timeout(message);
        if (cancellation.IsCancellationRequested())
            return StepResult::Cancel(message);
        return StepResult::Error(message);
    }

  private:
    std::shared_ptr<NativeExtensionRuntime> m_runtime;
    std::string m_typeId;
    nlohmann::json m_request;
    std::shared_ptr<NativeComponentInstance> m_component;
};

std::unique_ptr<ICommand> MakeNativeCommand(std::shared_ptr<NativeExtensionRuntime> runtime,
                                            const std::string &typeId)
{
    return std::make_unique<NativeCommandAdapter>(std::move(runtime), typeId);
}
std::unique_ptr<IInstrument> MakeNativeInstrument(std::shared_ptr<NativeExtensionRuntime> runtime,
                                                  const std::string &typeId)
{
    return std::make_unique<NativeInstrumentAdapter>(std::move(runtime), typeId);
}

} // namespace artest::extensions

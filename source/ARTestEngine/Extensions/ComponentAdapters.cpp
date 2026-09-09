#include "ComponentAdapters.h"

namespace artest::extensions
{
class ExtensionInstrumentAdapter final : public IInstrument
{
private:
    std::shared_ptr<IExtensionRuntime> m_runtime;
    std::string m_typeId;
    std::string m_id;
    std::shared_ptr<ComponentLease> m_component;

public:
    ExtensionInstrumentAdapter(std::shared_ptr<IExtensionRuntime> runtime, std::string typeId) noexcept
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

};

class ExtensionCommandAdapter final : public ICommand
{
private:
    std::shared_ptr<IExtensionRuntime> m_runtime;
    std::string m_typeId;
    nlohmann::json m_request;
    std::shared_ptr<ComponentLease> m_component;

public:
    ExtensionCommandAdapter(std::shared_ptr<IExtensionRuntime> runtime, std::string typeId) noexcept
        : m_runtime(std::move(runtime)), m_typeId(std::move(typeId))
    {
    }
    
    [[nodiscard]] std::string Name() const override
    {
        return m_typeId;
    }
    [[nodiscard]] OperationResult Configure(const nlohmann::json &parameters, std::shared_ptr<IInstrument> instrument) override
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
    [[nodiscard]] StepResult Execute(ExecutionContext &, const CancellationToken &cancellation) override
    {
        InvocationOutput response;
        const auto result = m_runtime->Invoke(m_component, "artest.command.execute.v1", m_request,
                                              &cancellation, &response);
        if (result.Succeeded())
        {
            const auto &data = response.data;
            auto step = StepResult::Pass(data.is_object()
                ? data.value("message", std::string{"Extension command passed."}) : "Extension command passed.");
            step.data = data;
            step.dataSchema = response.schemaId;
            if (response.schemaId == "artest.schema.command-result.v1")
            {
                if (!data.is_object() || !data.contains("verdict") || !data["verdict"].is_string() ||
                    !data.contains("data") || !data.contains("dataSchema") || !data["dataSchema"].is_string() ||
                    data["dataSchema"].get<std::string>().empty() ||
                    (data["verdict"] != "passed" && data["verdict"] != "failed"))
                    return StepResult::Error("EXTENSION_RESULT_INVALID: malformed command verdict.");
                step.status = data["verdict"] == "passed" ? StepStatus::Passed : StepStatus::Failed;
                step.data = data["data"];
                step.dataSchema = data["dataSchema"];
            }
            return step;
        }
        std::string message;
        auto status = StepStatus::Error;
        for (const auto &diagnostic : result.diagnostics)
        {
            if (!message.empty()) message += "; ";
            message += diagnostic.code + ": " + diagnostic.message;
            if (diagnostic.code == "EXTENSION_TIMED_OUT") status = StepStatus::TimedOut;
            if (diagnostic.code == "EXTENSION_CANCELLED") status = StepStatus::Cancelled;
        }
        if (cancellation.IsTimedOut())
            status = StepStatus::TimedOut;
        else if (cancellation.IsCancellationRequested())
            status = StepStatus::Cancelled;
        auto step = StepResult::Error(message.empty() ? "Extension command failed." : message);
        step.status = status;
        step.indeterminate = response.indeterminate;
        return step;
    }
  
};

std::unique_ptr<ICommand> MakeExtensionCommand(std::shared_ptr<IExtensionRuntime> runtime, const std::string &typeId)
{
    return std::make_unique<ExtensionCommandAdapter>(std::move(runtime), typeId);
}

std::unique_ptr<IInstrument> MakeExtensionInstrument(std::shared_ptr<IExtensionRuntime> runtime, const std::string &typeId)
{
    return std::make_unique<ExtensionInstrumentAdapter>(std::move(runtime), typeId);
}

} // namespace artest::extensions

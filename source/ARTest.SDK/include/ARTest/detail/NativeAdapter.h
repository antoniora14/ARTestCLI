#pragma once

#include "../Definition.h"
#include "NativeContext.h"

namespace artest::sdk::detail
{
template <Extension (*Define)()> class NativeAdapter final
{
    struct ExtensionState
    {
        const Extension &definition;
        ARTestHostApiV0 host;
    };
    enum class DriverState
    {
        Created,
        NeedsCleanup,
        Ready,
        Stopped
    };
    struct ComponentState
    {
        ExtensionState *owner;
        const Registration *registration;
        Json configuration;
        std::unique_ptr<Command> command;
        std::unique_ptr<InstrumentDriver> driver;
        DriverState state = DriverState::Created;
    };
    static const Extension &Definition()
    {
        // The definition function declares metadata/factories only. It creates no components.
        static const Extension definition = Define();
        if (DefinitionAccess::Components(definition).empty())
            throw std::invalid_argument("An extension must declare at least one component.");
        return definition;
    }
    static ExtensionState *State(ARTestExtensionHandle handle) noexcept
    {
        return reinterpret_cast<ExtensionState *>(handle);
    }
    static ComponentState *State(ARTestComponentHandle handle) noexcept
    {
        return reinterpret_cast<ComponentState *>(handle);
    }

  public:
    static ARTestStatus Query(std::uint32_t major, std::uint32_t minor,
                              ARTestExtensionApiV0 *output, ARTestErrorBuffer *error) noexcept
    {
        return Boundary(error, [&]() -> ARTestStatus {
            if (!output || output->struct_size < sizeof(*output))
                return Fail(error, Status::InvalidArgument,
                            "A complete extension API output table is required.");
            if (major != ARTEST_EXTENSION_ABI_MAJOR || minor > ARTEST_EXTENSION_ABI_MINOR)
                return Fail(error, Status::IncompatibleAbi,
                            "The requested native ABI is not supported.");
            const auto &definition = Definition();
            *output = {sizeof(*output),
                       major,
                       minor,
                       0,
                       View(DefinitionAccess::Id(definition)),
                       View(DefinitionAccess::Version(definition)),
                       &CreateExtension,
                       &DestroyExtension,
                       &Count,
                       &Describe,
                       &CreateComponent,
                       &DestroyComponent,
                       &Invoke};
            return ARTEST_STATUS_OK;
        });
    }

  private:
    static ARTestStatus ARTEST_ABI_CALL CreateExtension(const ARTestHostApiV0 *host,
                                                        const ARTestPayloadView *manifest,
                                                        ARTestExtensionHandle *output,
                                                        ARTestErrorBuffer *error) noexcept
    {
        if (output)
            *output = nullptr;
        return Boundary(error, [&]() -> ARTestStatus {
            if (!host || !output || host->struct_size < sizeof(*host) || host->reserved ||
                host->abi_major != ARTEST_EXTENSION_ABI_MAJOR ||
                host->abi_minor < ARTEST_EXTENSION_ABI_MINOR || !host->log ||
                !host->monotonic_time_ns || !host->resolve_service || !host->invoke_service ||
                !host->release_service)
                return Fail(error, Status::InvalidArgument,
                            "A compatible, complete host API and output handle are required.");
            if (!Parse(manifest).is_object())
                return Fail(error, Status::InvalidArgument,
                            "The validated manifest must be an object.");
            auto state = std::make_unique<ExtensionState>(ExtensionState{Definition(), *host});
            *output = reinterpret_cast<ARTestExtensionHandle>(state.release());
            return ARTEST_STATUS_OK;
        });
    }
    static void ARTEST_ABI_CALL DestroyExtension(ARTestExtensionHandle handle) noexcept
    {
        // Host must destroy components first; no cross-module delete is exposed.
        delete State(handle);
    }
    static std::size_t ARTEST_ABI_CALL Count(ARTestExtensionHandle handle) noexcept
    {
        return handle ? DefinitionAccess::Components(State(handle)->definition).size() : 0;
    }
    static ARTestStatus ARTEST_ABI_CALL Describe(ARTestExtensionHandle handle, std::size_t index,
                                                 ARTestComponentDescriptorV0 *output,
                                                 ARTestErrorBuffer *error) noexcept
    {
        return Boundary(error, [&]() -> ARTestStatus {
            if (!handle || !output || output->struct_size < sizeof(*output))
                return Fail(error, Status::InvalidArgument,
                            "A descriptor output and extension handle are required.");
            const auto &entries = DefinitionAccess::Components(State(handle)->definition);
            if (index >= entries.size())
                return Fail(error, Status::InvalidArgument, "Invalid component index.");
            const auto &entry = entries[index];
            *output = {
                sizeof(*output),
                entry.driverFactory ? ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER
                                    : ARTEST_COMPONENT_KIND_COMMAND,
                entry.simulated       ? ARTEST_COMPONENT_FLAG_SIMULATED
                : entry.driverFactory ? ARTEST_COMPONENT_FLAG_REQUIRES_HARDWARE
                                      : ARTEST_COMPONENT_FLAG_NONE,
                View(entry.id),
                View(entry.contract),
                View(entry.version),
                View(entry.name),
                {sizeof(ARTestPayloadView), ARTEST_PAYLOAD_ENCODING_UNSPECIFIED, {}, {}, {}}};
            return ARTEST_STATUS_OK;
        });
    }
    static ARTestStatus ARTEST_ABI_CALL CreateComponent(ARTestExtensionHandle handle,
                                                        ARTestStringView type,
                                                        const ARTestPayloadView *configuration,
                                                        ARTestComponentHandle *output,
                                                        ARTestErrorBuffer *error) noexcept
    {
        if (output)
            *output = nullptr;
        return Boundary(error, [&]() -> ARTestStatus {
            if (!handle || !output)
                return Fail(error, Status::InvalidArgument,
                            "Extension and component output handles are required.");
            const auto id = Text(type);
            auto *owner = State(handle);
            for (const auto &entry : DefinitionAccess::Components(owner->definition))
            {
                if (entry.id != id)
                    continue;
                auto values = Parse(configuration);
                if (!values.is_object())
                    return Fail(error, Status::InvalidArgument,
                                "Component configuration must be an object.");
                auto component = std::make_unique<ComponentState>();
                component->owner = owner;
                component->registration = &entry;
                component->configuration = std::move(values);
                if (entry.commandFactory)
                    component->command = entry.commandFactory();
                else
                    component->driver = entry.driverFactory();
                *output = reinterpret_cast<ARTestComponentHandle>(component.release());
                return ARTEST_STATUS_OK;
            }
            return Fail(error, Status::NotFound, "Unknown component type.");
        });
    }
    static void ARTEST_ABI_CALL DestroyComponent(ARTestExtensionHandle owner,
                                                 ARTestComponentHandle handle) noexcept
    {
        if (handle && State(handle)->owner == State(owner))
            delete State(handle);
    }
    static ARTestStatus ARTEST_ABI_CALL
    Invoke(ARTestExtensionHandle owner, ARTestComponentHandle handle, ARTestStringView operation,
           const ARTestPayloadView *payload, const ARTestInvocationContextV0 *invocation,
           const ARTestResultSinkV0 *sink, ARTestErrorBuffer *error) noexcept
    {
        return Boundary(error, [&]() -> ARTestStatus {
            if (!owner || !handle || State(handle)->owner != State(owner) || !invocation ||
                invocation->struct_size < sizeof(*invocation) || invocation->reserved ||
                !ValidSink(sink))
                return Fail(error, Status::InvalidArgument,
                            "Valid matching handles, invocation and result sink are required.");
            auto &component = *State(handle);
            const auto action = Text(operation);
            const auto request = Parse(payload);
            if (!request.is_object())
                return Fail(error, Status::InvalidArgument,
                            "Invocation request must be an object.");
            if (component.command)
            {
                if (action != "artest.command.execute.v1" &&
                    action != "artest.component.validate.v1")
                    return Fail(error, Status::OperationNotSupported, "Unknown command operation.");
                const Parameters envelope{request};
                const auto values = envelope.Get<Json>("parameters");
                const Parameters parameters{values};
                const auto instrument = envelope.Optional<std::string>("instrumentId", {});
                if (instrument.find('\0') != std::string::npos)
                    return Fail(error, Status::InvalidArgument,
                                "Instrument ID contains a null byte.");
                NativeContext context{component.owner->host, *invocation,
                                      component.registration->id, instrument};
                if (auto status = context.Checkpoint(); !status)
                    return Return(status, sink, error);
                auto result = component.command->Validate(parameters);
                if (result && action == "artest.command.execute.v1")
                    result = component.command->Execute(parameters, context);
                return Return(result, sink, error);
            }
            const bool shutdown = action == "artest.lifecycle.shutdown.v1";
            NativeContext context{
                component.owner->host, *invocation, component.registration->id, {}, shutdown};
            if (shutdown)
            {
                if (component.state == DriverState::Stopped)
                    return ARTEST_STATUS_OK;
                component.state = DriverState::Stopped;
                return Return(component.driver->Shutdown(context), sink, error);
            }
            if (auto status = context.Checkpoint(); !status)
                return Return(status, sink, error);
            if (action == "artest.lifecycle.initialize.v1")
            {
                if (component.state != DriverState::Created)
                    return Fail(error, Status::InvalidState,
                                "Driver initialization may run only once per instance.");
                // Set before calling user code: a throw or failure still requires Shutdown.
                component.state = DriverState::NeedsCleanup;
                auto result =
                    component.driver->Initialize(Parameters{component.configuration}, context);
                if (result)
                    component.state = DriverState::Ready;
                return Return(result, sink, error);
            }
            if (component.state != DriverState::Ready)
                return Fail(error, Status::InvalidState, "The driver is not initialized.");
            return Return(component.driver->Dispatch(action, Parameters{request}, context), sink,
                          error);
        });
    }
};
} // namespace artest::sdk::detail

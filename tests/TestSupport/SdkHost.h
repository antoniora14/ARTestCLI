#pragma once
#include <ARTest/Extension.h>
#include <vector>

// Deliberately fault-injectable ABI host, used only by SDK contract tests.
namespace sdk_tests
{
using namespace artest::sdk;
struct Host
{
    std::uint64_t now = 100;
    bool cancelled = false, throwResolve = false, throwInvoke = false, throwRelease = false;
    bool returnHandleOnFailure = false, malformedResult = false, doubleResult = false;
    ARTestStatus resolveStatus = ARTEST_STATUS_OK, invokeStatus = ARTEST_STATUS_OK;
    unsigned resolved = 0, released = 0, invoked = 0;
    std::string lastContract, lastInstance, lastOperation;
    std::string responseSchema = "artest.schema.generic-json.v1";
    Json lastRequest;
    ARTestInvocationContextV0 lastInvocation{};
    std::vector<std::string> logs;

    static void ARTEST_ABI_CALL Log(void *state, ARTestLogSeverity, ARTestStringView,
                                    ARTestStringView message)
    {
        static_cast<Host *>(state)->logs.emplace_back(detail::Text(message));
    }
    static std::uint64_t ARTEST_ABI_CALL Clock(void *state)
    {
        return static_cast<Host *>(state)->now;
    }
    static ARTestBool32 ARTEST_ABI_CALL Cancel(void *state)
    {
        return static_cast<Host *>(state)->cancelled ? ARTEST_TRUE : ARTEST_FALSE;
    }
    static ARTestStatus ARTEST_ABI_CALL Resolve(void *state, ARTestStringView contract,
                                                ARTestStringView instance,
                                                ARTestServiceHandle *output, ARTestErrorBuffer *)
    {
        auto &self = *static_cast<Host *>(state);
        ++self.resolved;
        self.lastContract = detail::Text(contract);
        self.lastInstance = detail::Text(instance);
        if (self.resolveStatus == ARTEST_STATUS_OK || self.returnHandleOnFailure)
            *output = reinterpret_cast<ARTestServiceHandle>(state);
        if (self.throwResolve)
            throw std::runtime_error("Resolve failed");
        return self.resolveStatus;
    }
    static ARTestStatus ARTEST_ABI_CALL Invoke(void *state, ARTestServiceHandle,
                                               ARTestStringView operation,
                                               const ARTestPayloadView *request,
                                               const ARTestInvocationContextV0 *invocation,
                                               const ARTestResultSinkV0 *sink,
                                               ARTestErrorBuffer *error)
    {
        auto &self = *static_cast<Host *>(state);
        ++self.invoked;
        self.lastOperation = detail::Text(operation);
        self.lastRequest = detail::Parse(request);
        self.lastInvocation = *invocation;
        if (self.throwInvoke)
            throw std::runtime_error("Invoke failed");
        if (self.invokeStatus != ARTEST_STATUS_OK)
            return self.invokeStatus;
        const std::string text = self.malformedResult ? "{broken" : "{\"value\":42}";
        auto payload = detail::Payload(text);
        payload.schema_id = detail::View(self.responseSchema);
        (void)sink->write(sink->sink_context, &payload, error);
        if (self.doubleResult)
            (void)sink->write(sink->sink_context, &payload, error);
        return ARTEST_STATUS_OK; // Intentionally ignore sink failures to test SDK defense.
    }
    static void ARTEST_ABI_CALL Release(void *state, ARTestServiceHandle)
    {
        auto &self = *static_cast<Host *>(state);
        ++self.released;
        if (self.throwRelease)
            throw std::runtime_error("Release failed");
    }
    ARTestHostApiV0 Api()
    {
        return {sizeof(ARTestHostApiV0), 0, 1, 0, this, &Log, &Clock, &Resolve, &Invoke, &Release};
    }
    ARTestInvocationContextV0 Invocation()
    {
        return {sizeof(ARTestInvocationContextV0), 0, 42, 0, this, &Cancel};
    }
};

template <Extension (*Define)()> struct Harness
{
    Host host;
    ARTestExtensionApiV0 api{};
    ARTestExtensionHandle extension = nullptr;
    std::vector<ARTestComponentHandle> components;
    detail::ErrorStorage error;
    Json output;
    std::string schemaId;
    unsigned writes = 0;

    Harness()
    {
        api.struct_size = sizeof(api);
        if (detail::NativeAdapter<Define>::Query(0, 1, &api, &error.value) != ARTEST_STATUS_OK)
            throw std::runtime_error("Query failed");
        auto hostApi = host.Api();
        if (api.create_extension(&hostApi, nullptr, &extension, &error.value) != ARTEST_STATUS_OK)
            throw std::runtime_error("CreateExtension failed");
    }
    ~Harness()
    {
        for (const auto component : components)
            api.destroy_component(extension, component);
        api.destroy_extension(extension);
    }
    Harness(const Harness &) = delete;
    Harness &operator=(const Harness &) = delete;
    ARTestComponentHandle Create(std::string_view type, const Json &configuration = Json::object())
    {
        const auto text = configuration.dump();
        const auto payload = detail::Payload(text);
        ARTestComponentHandle component = nullptr;
        if (api.create_component(extension, detail::View(type), &payload, &component,
                                 &error.value) != ARTEST_STATUS_OK)
            throw std::runtime_error(error.Message());
        components.push_back(component);
        return component;
    }
    static ARTestStatus ARTEST_ABI_CALL Capture(void *state, const ARTestPayloadView *payload,
                                                ARTestErrorBuffer *error) noexcept
    {
        return detail::Boundary(error, [&]() -> ARTestStatus {
            auto &self = *static_cast<Harness *>(state);
            ++self.writes;
            self.output = detail::Parse(payload);
            self.schemaId = detail::Text(payload->schema_id);
            return ARTEST_STATUS_OK;
        });
    }
    ARTestStatus Invoke(ARTestComponentHandle component, std::string_view operation,
                        const Json &request = Json::object(), std::uint64_t deadline = 0)
    {
        const auto text = request.dump();
        const auto payload = detail::Payload(text);
        auto invocation = host.Invocation();
        invocation.deadline_monotonic_ns = deadline;
        ARTestResultSinkV0 sink{sizeof(ARTestResultSinkV0), 0, this, &Capture};
        output = nullptr;
        schemaId.clear();
        writes = 0;
        return api.invoke_component(extension, component, detail::View(operation), &payload,
                                    &invocation, &sink, &error.value);
    }
};
} // namespace sdk_tests

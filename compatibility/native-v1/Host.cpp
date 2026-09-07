#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ARTestEngineClient.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

using Json = nlohmann::json;
using artest::sdk::EngineClient;

namespace
{
void Require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}
void Check(const artest::sdk::ClientStatus &status)
{
    Require(status.Succeeded(), status.message);
}
Json Plan()
{
    return {{"format", "ARTest.Script"}, {"version", 1},
        {"instruments", Json::array({
            {{"id", "I1"}, {"type", "com.artest.compat.driver.value"}, {"config", {{"value", 11}}}},
            {{"id", "I2"}, {"type", "com.artest.compat.driver.value"}, {"config", {{"value", 29}}}}})},
        {"commands", Json::array({
            {{"stepId", 1}, {"name", "com.artest.compat.command.read"}, {"instrument", "I1"}, {"params", Json::object()}},
            {{"stepId", 2}, {"name", "com.artest.compat.command.read"}, {"instrument", "I2"}, {"params", Json::object()}},
            {{"stepId", 3}, {"name", "com.artest.compat.command.read"}, {"instrument", "I1"}, {"params", Json::object()}}})}};
}
struct Events
{
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> values;
    bool executing = false;
    void Add(std::string_view event)
    {
        std::lock_guard lock{mutex};
        values.emplace_back(event);
        executing = executing || event.find("COMPAT_EXECUTE") != std::string_view::npos;
        changed.notify_all();
    }
    std::size_t Count(std::string_view marker)
    {
        std::lock_guard lock{mutex};
        return static_cast<std::size_t>(std::count_if(values.begin(), values.end(),
            [marker](const auto &value) { return value.find(marker) != std::string::npos; }));
    }
};

Json Run(const std::string &scenario, const std::string &catalog)
{
    const std::vector<std::string> allowed{"Lifecycle", "ServiceFailure", "CleanupFailure",
        "Cancellation", "Timeout", "InvalidParameters", "IncompatibleAbi", "IntegrityMismatch"};
    Require(std::find(allowed.begin(), allowed.end(), scenario) != allowed.end(), "Unknown scenario");
    // Events outlive the client: its destructor retires the subscription/session.
    Events events;
    EngineClient client;
    Check(client.Create(R"({"loadDefaultCatalog":false})"));
    Check(client.SubscribeEvents([&events](std::string_view value) { events.Add(value); }));
    const auto prepared = client.PrepareCatalog(catalog);
    if (scenario == "IncompatibleAbi" || scenario == "IntegrityMismatch")
    {
        const auto diagnostic = scenario == "IncompatibleAbi" ? "EXTENSION_RUNTIME_INCOMPATIBLE" : "EXTENSION_INTEGRITY_MISMATCH";
        Require(!prepared.Succeeded(), "Incompatible package was accepted");
        Require(prepared.message.find(diagnostic) != std::string::npos, prepared.message);
        Require(events.Count("COMPAT_INIT") == 0, "Rejected catalog initialized a driver");
        return {{"diagnostic", prepared.message}, {"initialized", 0}};
    }
    Check(prepared);
    Require(events.Count("COMPAT_INIT") == 0, "Preparation initialized a driver");
    auto plan = Plan();
    if (scenario == "InvalidParameters")
    {
        plan["commands"][0]["params"]["waitMs"] = -1;
        const auto result = client.Compile(plan.dump());
        Require(!result.Succeeded() && result.message.find("PARAMETER_RANGE_INVALID") != std::string::npos,
                "Invalid input was not rejected with PARAMETER_RANGE_INVALID: " + result.message);
        Require(!client.Start().Succeeded(), "Invalid plan started");
        Require(events.Count("COMPAT_INIT") == 0, "Invalid input initialized a driver");
        return {{"diagnostic", result.message}, {"initialized", 0}};
    }
    if (scenario == "ServiceFailure") plan["instruments"][0]["config"]["failRead"] = true;
    if (scenario == "CleanupFailure") plan["instruments"][0]["config"]["failCleanup"] = true;
    if (scenario == "Cancellation" || scenario == "Timeout") plan["commands"][0]["params"]["waitMs"] = 5000;
    if (scenario == "Timeout")
        plan["commands"][0]["policy"] = {{"timeoutMs", 20}, {"maxAttempts", 1}, {"onFailure", "stop"}};
    Check(client.Compile(plan.dump()));
    Require(events.Count("COMPAT_INIT") == 0, "Compilation initialized a driver");
    const auto started = std::chrono::steady_clock::now();
    Check(client.Start());
    if (scenario == "Cancellation")
    {
        std::unique_lock lock{events.mutex};
        Require(events.changed.wait_for(lock, std::chrono::seconds{3}, [&] { return events.executing; }),
                "Command did not enter execution");
        lock.unlock(); // Never call the Engine while holding a callback's lock.
        Check(client.Cancel());
    }
    bool completed = false;
    Check(client.Wait(10000, completed));
    Require(completed, "Session exceeded the host wait limit");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    std::string text;
    Check(client.SerializeResult(text));
    const auto result = Json::parse(text);
    Require(events.Count("COMPAT_INIT") == 2, "Expected two independent initializations");
    Require(events.Count("COMPAT_CLEANUP_ATTEMPT") == 2, "Expected exactly two cleanup attempts");
    if (scenario == "Lifecycle")
    {
        Require(result.at("status") == "passed" && result.at("summary").at("passedSteps") == 3,
                "Successful run verdict/count mismatch");
        const std::vector<std::string> expected{"COMPAT_VALUE=11", "COMPAT_VALUE=29", "COMPAT_VALUE=11"};
        Require(result.at("steps").size() == expected.size(), "Wrong number of steps");
        for (std::size_t i = 0; i < expected.size(); ++i)
            Require(result["steps"][i].dump().find(expected[i]) != std::string::npos,
                    "Configured instance/result routing mismatch");
        Check(client.Restart());
        completed = false;
        Check(client.Wait(10000, completed));
        Require(completed, "Restart did not finish");
        Check(client.SerializeResult(text));
        Require(Json::parse(text).at("status") == "passed", "Fresh-session restart failed");
        Require(events.Count("COMPAT_INIT") == 4 && events.Count("COMPAT_CLEANUP_ATTEMPT") == 4,
                "Fresh-session lifecycle counts mismatch");
    }
    else
    {
        Require(result.at("status") != "passed", "Failure became PASSED");
        if (scenario == "ServiceFailure")
            Require(result.dump().find("COMPAT_SERVICE_FAILURE") != std::string::npos, "Lost service diagnostic");
        if (scenario == "CleanupFailure")
            Require(result.dump().find("COMPAT_CLEANUP_FAILURE") != std::string::npos, "Lost cleanup diagnostic");
        if (scenario == "Cancellation") Require(result.at("status") == "cancelled", "Cancellation verdict mismatch");
        if (scenario == "Timeout")
            Require(result.at("summary").at("timedOutSteps") == 1, "Timeout verdict mismatch");
        if (scenario == "Cancellation" || scenario == "Timeout")
            Require(elapsed < std::chrono::seconds{3}, "Cooperative interruption took the full device wait");
    }
    return {{"run", result}, {"initialized", events.Count("COMPAT_INIT")},
            {"cleanupAttempts", events.Count("COMPAT_CLEANUP_ATTEMPT")}};
}
}

int wmain(int argc, wchar_t **argv)
{
    Json report{{"format", "ARTest.NativeCompatibilityCase"}, {"version", 1}, {"passed", false},
                {"compiler", _MSC_FULL_VER}, {"compiledSdk", ARTEST_COMPAT_SDK_VERSION}};
    try
    {
        Require(argc == 3, "Usage: ARTestCompatHost <catalog> <scenario>");
        const auto utf8 = std::filesystem::absolute(argv[1]).u8string();
        const std::string scenario = std::filesystem::path{argv[2]}.string();
        report["scenario"] = scenario;
        wchar_t enginePath[32768]{};
        Require(GetModuleFileNameW(GetModuleHandleW(L"ARTestEngine.dll"), enginePath, 32768) != 0,
                "Cannot identify the loaded Engine");
        const auto engineUtf8 = std::filesystem::path{enginePath}.u8string();
        report["enginePath"] = std::string(engineUtf8.begin(), engineUtf8.end());
        report["evidence"] = Run(scenario, std::string(utf8.begin(), utf8.end()));
        report["passed"] = true;
    }
    catch (const std::exception &error) { report["error"] = error.what(); }
    std::cout << report.dump() << '\n';
    return report["passed"] == true ? 0 : 1;
}

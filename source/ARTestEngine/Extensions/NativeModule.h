#pragma once
#include "../../ThirdParty/json.hpp"
#include "NativeAbiSupport.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>
namespace artest::extensions
{
struct ComponentRecord
{
    ARTestComponentKind kind = 0U;
    ARTestComponentFlags flags = ARTEST_COMPONENT_FLAG_NONE;
    std::string typeId;
    std::string contractId;
    std::string version;
    std::string displayName;
};

struct NativeModule
{
    ~NativeModule()
    {
        if (extension != nullptr && api.destroy_extension != nullptr)
            api.destroy_extension(extension);
        if (library != nullptr)
            FreeLibrary(library);
    }
    std::filesystem::path packageRoot;
    nlohmann::json manifest;
    std::string manifestText;
    std::string extensionId;
    HMODULE library = nullptr;
    ARTestExtensionApiV0 api{};
    ARTestExtensionHandle extension = nullptr;
    std::vector<ComponentRecord> components;
    mutable std::recursive_mutex invocationMutex;
};

class NativeComponentInstance final
{
  public:
    NativeComponentInstance(std::shared_ptr<NativeModule> owner, ComponentRecord descriptor,
                            ARTestComponentHandle value) noexcept
        : module(std::move(owner)), record(std::move(descriptor)), handle(value)
    {
    }
    ~NativeComponentInstance()
    {
        if (handle != nullptr)
        {
            std::scoped_lock lock{module->invocationMutex};
            module->api.destroy_component(module->extension, handle);
        }
    }
    std::shared_ptr<NativeModule> module;
    ComponentRecord record;
    ARTestComponentHandle handle = nullptr;
};

using NativeTypeMap =
    std::unordered_map<std::string, std::pair<std::shared_ptr<NativeModule>, ComponentRecord>>;

} // namespace artest::extensions

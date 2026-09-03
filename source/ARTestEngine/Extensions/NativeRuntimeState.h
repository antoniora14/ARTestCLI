#pragma once
#include "../../ARTestEngine.Core/Catalog/RegistryTransaction.h"
#include "ExtensionCatalog.h"
#include "NativeExtensionRuntime.h"
#include "NativeServiceBroker.h"
namespace artest::extensions
{
class NativeExtensionRuntime::Implementation
{
  public:
    explicit Implementation(IEventSink &sink) : eventSink(sink), broker(sink)
    {
    }
    IEventSink &eventSink;
    NativeServiceBroker broker;
    std::vector<std::shared_ptr<NativeModule>> modules;
    NativeTypeMap types;
    ExtensionCatalog catalog;
    CatalogScan lastScan;
    std::string catalogStatus = "notLoaded";
    std::uint64_t catalogGeneration = 0U;
    RegistrationToken registration;
    bool activating = false;
    mutable std::mutex catalogMutex;
};

} // namespace artest::extensions

#ifndef ARTEST_EXTENSION_ABI_V0_H
#define ARTEST_EXTENSION_ABI_V0_H

/*
 * Experimental ARTest native extension ABI for the Stage D D1 prototype.
 * ABI major zero is not stable and must not be used as a compatibility promise.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define ARTEST_ABI_CALL __cdecl
#if defined(ARTEST_EXTENSION_EXPORTS)
#define ARTEST_ABI_EXPORT __declspec(dllexport)
#else
#define ARTEST_ABI_EXPORT
#endif
#else
#define ARTEST_ABI_CALL
#if defined(ARTEST_EXTENSION_EXPORTS)
#define ARTEST_ABI_EXPORT __attribute__((visibility("default")))
#else
#define ARTEST_ABI_EXPORT
#endif
#endif

#define ARTEST_EXTENSION_ABI_MAJOR UINT32_C(0)
#define ARTEST_EXTENSION_ABI_MINOR UINT32_C(1)

typedef int32_t ARTestStatus;
typedef uint32_t ARTestBool32;
typedef uint32_t ARTestComponentKind;
typedef uint32_t ARTestPayloadEncoding;
typedef uint32_t ARTestLogSeverity;
typedef uint64_t ARTestComponentFlags;

#define ARTEST_FALSE UINT32_C(0)
#define ARTEST_TRUE UINT32_C(1)

#define ARTEST_STATUS_OK INT32_C(0)
#define ARTEST_STATUS_INVALID_ARGUMENT INT32_C(1)
#define ARTEST_STATUS_INCOMPATIBLE_ABI INT32_C(2)
#define ARTEST_STATUS_BUFFER_TOO_SMALL INT32_C(3)
#define ARTEST_STATUS_NOT_FOUND INT32_C(4)
#define ARTEST_STATUS_ALREADY_EXISTS INT32_C(5)
#define ARTEST_STATUS_INVALID_STATE INT32_C(6)
#define ARTEST_STATUS_OPERATION_NOT_SUPPORTED INT32_C(7)
#define ARTEST_STATUS_CANCELLED INT32_C(8)
#define ARTEST_STATUS_TIMED_OUT INT32_C(9)
#define ARTEST_STATUS_RESOURCE_UNAVAILABLE INT32_C(10)
#define ARTEST_STATUS_EXTENSION_FAILURE INT32_C(11)
#define ARTEST_STATUS_HOST_FAILURE INT32_C(12)
#define ARTEST_STATUS_INTERNAL_FAILURE INT32_C(13)

#define ARTEST_COMPONENT_KIND_COMMAND UINT32_C(1)
#define ARTEST_COMPONENT_KIND_INSTRUMENT_DRIVER UINT32_C(2)
#define ARTEST_COMPONENT_KIND_TOOL UINT32_C(3)

#define ARTEST_PAYLOAD_ENCODING_UNSPECIFIED UINT32_C(0)
#define ARTEST_PAYLOAD_ENCODING_JSON_UTF8 UINT32_C(1)
#define ARTEST_PAYLOAD_ENCODING_BINARY UINT32_C(2)

#define ARTEST_LOG_TRACE UINT32_C(0)
#define ARTEST_LOG_INFORMATION UINT32_C(1)
#define ARTEST_LOG_WARNING UINT32_C(2)
#define ARTEST_LOG_ERROR UINT32_C(3)

#define ARTEST_COMPONENT_FLAG_NONE UINT64_C(0)
#define ARTEST_COMPONENT_FLAG_SIMULATED UINT64_C(1)
#define ARTEST_COMPONENT_FLAG_REQUIRES_HARDWARE UINT64_C(2)

#pragma pack(push, 8)

typedef struct ARTestExtensionOpaque* ARTestExtensionHandle;
typedef struct ARTestComponentOpaque* ARTestComponentHandle;
typedef struct ARTestServiceOpaque* ARTestServiceHandle;

typedef struct ARTestStringView
{
    const char* data;
    size_t size;
} ARTestStringView;

typedef struct ARTestByteView
{
    const uint8_t* data;
    size_t size;
} ARTestByteView;

typedef struct ARTestErrorBuffer
{
    uint32_t struct_size;
    uint32_t reserved;
    char* data;
    size_t capacity;
    size_t required_size;
} ARTestErrorBuffer;

typedef struct ARTestPayloadView
{
    uint32_t struct_size;
    ARTestPayloadEncoding encoding;
    ARTestStringView schema_id;
    ARTestStringView media_type;
    ARTestByteView bytes;
} ARTestPayloadView;

typedef ARTestBool32 (ARTEST_ABI_CALL *ARTestIsCancellationRequestedFn)(
    void* cancellation_context);

typedef struct ARTestInvocationContextV0
{
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t invocation_id;
    uint64_t deadline_monotonic_ns;
    void* cancellation_context;
    ARTestIsCancellationRequestedFn is_cancellation_requested;
} ARTestInvocationContextV0;

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestWriteResultFn)(
    void* sink_context,
    const ARTestPayloadView* payload,
    ARTestErrorBuffer* error);

typedef struct ARTestResultSinkV0
{
    uint32_t struct_size;
    uint32_t reserved;
    void* sink_context;
    ARTestWriteResultFn write;
} ARTestResultSinkV0;

typedef struct ARTestComponentDescriptorV0
{
    uint32_t struct_size;
    ARTestComponentKind kind;
    ARTestComponentFlags flags;
    ARTestStringView type_id;
    ARTestStringView contract_id;
    ARTestStringView component_version;
    ARTestStringView display_name;
    ARTestPayloadView configuration_schema;
} ARTestComponentDescriptorV0;

typedef void (ARTEST_ABI_CALL *ARTestLogFn)(
    void* host_context,
    ARTestLogSeverity severity,
    ARTestStringView category,
    ARTestStringView message);

typedef uint64_t (ARTEST_ABI_CALL *ARTestMonotonicTimeNsFn)(
    void* host_context);

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestResolveServiceFn)(
    void* host_context,
    ARTestStringView contract_id,
    ARTestStringView configured_instance_id,
    ARTestServiceHandle* service,
    ARTestErrorBuffer* error);

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestInvokeServiceFn)(
    void* host_context,
    ARTestServiceHandle service,
    ARTestStringView operation_id,
    const ARTestPayloadView* request,
    const ARTestInvocationContextV0* invocation,
    const ARTestResultSinkV0* result_sink,
    ARTestErrorBuffer* error);

typedef void (ARTEST_ABI_CALL *ARTestReleaseServiceFn)(
    void* host_context,
    ARTestServiceHandle service);

typedef struct ARTestHostApiV0
{
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;
    void* host_context;
    ARTestLogFn log;
    ARTestMonotonicTimeNsFn monotonic_time_ns;
    ARTestResolveServiceFn resolve_service;
    ARTestInvokeServiceFn invoke_service;
    ARTestReleaseServiceFn release_service;
} ARTestHostApiV0;

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestCreateExtensionFn)(
    const ARTestHostApiV0* host_api,
    const ARTestPayloadView* validated_manifest,
    ARTestExtensionHandle* extension,
    ARTestErrorBuffer* error);

typedef void (ARTEST_ABI_CALL *ARTestDestroyExtensionFn)(
    ARTestExtensionHandle extension);

typedef size_t (ARTEST_ABI_CALL *ARTestGetComponentTypeCountFn)(
    ARTestExtensionHandle extension);

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestGetComponentDescriptorFn)(
    ARTestExtensionHandle extension,
    size_t component_index,
    ARTestComponentDescriptorV0* descriptor,
    ARTestErrorBuffer* error);

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestCreateComponentFn)(
    ARTestExtensionHandle extension,
    ARTestStringView type_id,
    const ARTestPayloadView* configuration,
    ARTestComponentHandle* component,
    ARTestErrorBuffer* error);

typedef void (ARTEST_ABI_CALL *ARTestDestroyComponentFn)(
    ARTestExtensionHandle extension,
    ARTestComponentHandle component);

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestInvokeComponentFn)(
    ARTestExtensionHandle extension,
    ARTestComponentHandle component,
    ARTestStringView operation_id,
    const ARTestPayloadView* request,
    const ARTestInvocationContextV0* invocation,
    const ARTestResultSinkV0* result_sink,
    ARTestErrorBuffer* error);

typedef struct ARTestExtensionApiV0
{
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;
    ARTestStringView extension_id;
    ARTestStringView extension_version;
    ARTestCreateExtensionFn create_extension;
    ARTestDestroyExtensionFn destroy_extension;
    ARTestGetComponentTypeCountFn get_component_type_count;
    ARTestGetComponentDescriptorFn get_component_descriptor;
    ARTestCreateComponentFn create_component;
    ARTestDestroyComponentFn destroy_component;
    ARTestInvokeComponentFn invoke_component;
} ARTestExtensionApiV0;

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestExtensionQueryFn)(
    uint32_t requested_abi_major,
    uint32_t requested_abi_minor,
    ARTestExtensionApiV0* extension_api,
    ARTestErrorBuffer* error);

#pragma pack(pop)

#ifdef __cplusplus
extern "C"
{
#endif

ARTEST_ABI_EXPORT ARTestStatus ARTEST_ABI_CALL ARTestExtension_Query(
    uint32_t requested_abi_major,
    uint32_t requested_abi_minor,
    ARTestExtensionApiV0* extension_api,
    ARTestErrorBuffer* error);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(ARTestStatus) == 4, "ARTestStatus must be 32 bits.");
static_assert(sizeof(ARTestBool32) == 4, "ARTestBool32 must be 32 bits.");
#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(ARTestStringView) == 16, "Unexpected x64 ARTestStringView layout.");
static_assert(sizeof(ARTestErrorBuffer) == 32, "Unexpected x64 ARTestErrorBuffer layout.");
static_assert(sizeof(ARTestPayloadView) == 56, "Unexpected x64 ARTestPayloadView layout.");
static_assert(sizeof(ARTestInvocationContextV0) == 40, "Unexpected x64 invocation layout.");
static_assert(sizeof(ARTestResultSinkV0) == 24, "Unexpected x64 result-sink layout.");
static_assert(sizeof(ARTestComponentDescriptorV0) == 136, "Unexpected x64 component descriptor layout.");
static_assert(sizeof(ARTestHostApiV0) == 64, "Unexpected x64 host API layout.");
static_assert(sizeof(ARTestExtensionApiV0) == 104, "Unexpected x64 extension API layout.");
#endif
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(ARTestStatus) == 4, "ARTestStatus must be 32 bits.");
_Static_assert(sizeof(ARTestBool32) == 4, "ARTestBool32 must be 32 bits.");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(ARTestStringView) == 16, "Unexpected x64 ARTestStringView layout.");
_Static_assert(sizeof(ARTestErrorBuffer) == 32, "Unexpected x64 ARTestErrorBuffer layout.");
_Static_assert(sizeof(ARTestPayloadView) == 56, "Unexpected x64 ARTestPayloadView layout.");
_Static_assert(sizeof(ARTestInvocationContextV0) == 40, "Unexpected x64 invocation layout.");
_Static_assert(sizeof(ARTestResultSinkV0) == 24, "Unexpected x64 result-sink layout.");
_Static_assert(sizeof(ARTestComponentDescriptorV0) == 136, "Unexpected x64 component descriptor layout.");
_Static_assert(sizeof(ARTestHostApiV0) == 64, "Unexpected x64 host API layout.");
_Static_assert(sizeof(ARTestExtensionApiV0) == 104, "Unexpected x64 extension API layout.");
#endif
#endif

#endif /* ARTEST_EXTENSION_ABI_V0_H */

#ifndef ARTEST_ENGINE_API_H
#define ARTEST_ENGINE_API_H

#include "ARTestExtensionAbi.h"

#if defined(_WIN32) && defined(ARTEST_ENGINE_EXPORTS)
#define ARTEST_ENGINE_EXPORT __declspec(dllexport)
#else
#define ARTEST_ENGINE_EXPORT
#endif

#define ARTEST_ENGINE_API_MAJOR UINT32_C(0)
#define ARTEST_ENGINE_API_MINOR UINT32_C(4)
#define ARTEST_ENGINE_API_V0_1_SIZE ((uint32_t)144)
#define ARTEST_ENGINE_API_V0_2_SIZE ((uint32_t)160)
#define ARTEST_ENGINE_API_V0_3_SIZE ((uint32_t)168)

typedef struct ARTestEngineOpaque* ARTestEngineHandle;
typedef struct ARTestCompiledPlanOpaque* ARTestCompiledPlanHandle;
typedef struct ARTestSessionOpaque* ARTestSessionHandle;
typedef struct ARTestResultOpaque* ARTestResultHandle;
typedef struct ARTestSubscriptionOpaque* ARTestSubscriptionHandle;

typedef uint32_t ARTestSessionState;
#define ARTEST_SESSION_IDLE UINT32_C(0)
#define ARTEST_SESSION_INITIALIZING UINT32_C(1)
#define ARTEST_SESSION_RUNNING UINT32_C(2)
#define ARTEST_SESSION_CANCELLING UINT32_C(3)
#define ARTEST_SESSION_CLEANING_UP UINT32_C(4)
#define ARTEST_SESSION_COMPLETED UINT32_C(5)
#define ARTEST_SESSION_FAILED UINT32_C(6)
#define ARTEST_SESSION_CANCELLED UINT32_C(7)
#define ARTEST_SESSION_TIMED_OUT UINT32_C(8)

typedef uint32_t ARTestEngineEventKind;
#define ARTEST_ENGINE_EVENT_DIAGNOSTIC UINT32_C(0)
#define ARTEST_ENGINE_EVENT_INSTRUMENT_INITIALIZING UINT32_C(1)
#define ARTEST_ENGINE_EVENT_INSTRUMENT_INITIALIZED UINT32_C(2)
#define ARTEST_ENGINE_EVENT_INSTRUMENT_SHUTDOWN UINT32_C(3)
#define ARTEST_ENGINE_EVENT_INSTRUMENT_OPERATION UINT32_C(4)
#define ARTEST_ENGINE_EVENT_RUN_STATE_CHANGED UINT32_C(5)
#define ARTEST_ENGINE_EVENT_STEP_STARTED UINT32_C(6)
#define ARTEST_ENGINE_EVENT_STEP_ATTEMPT_STARTED UINT32_C(7)
#define ARTEST_ENGINE_EVENT_STEP_RETRY_SCHEDULED UINT32_C(8)
#define ARTEST_ENGINE_EVENT_STEP_COMPLETED UINT32_C(9)
#define ARTEST_ENGINE_EVENT_RUN_COMPLETED UINT32_C(10)

typedef uint32_t ARTestExecutionDecision;
#define ARTEST_EXECUTION_CONTINUE UINT32_C(0)
#define ARTEST_EXECUTION_CANCEL UINT32_C(1)

#pragma pack(push, 8)
typedef void (ARTEST_ABI_CALL *ARTestEngineEventFn)(void*, const ARTestPayloadView*);

typedef struct ARTestStepExecutionInfoV0
{
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t command_index;
    uint64_t step_id;
    ARTestStringView command_name;
} ARTestStepExecutionInfoV0;

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestBeforeStepFn)(void*, const ARTestStepExecutionInfoV0*, ARTestExecutionDecision*, ARTestErrorBuffer*);

typedef struct ARTestSessionOptionsV0
{
    uint32_t struct_size;
    uint32_t reserved;
    ARTestBeforeStepFn before_step;
    void* control_context;
} ARTestSessionOptionsV0;

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestCreateEngineFn)(const ARTestPayloadView*, ARTestEngineHandle*, ARTestErrorBuffer*);
typedef void (ARTEST_ABI_CALL *ARTestDestroyEngineFn)(ARTestEngineHandle);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestRefreshCatalogFn)(ARTestEngineHandle, ARTestStringView, ARTestErrorBuffer*);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestGetCatalogSnapshotFn)(ARTestEngineHandle, const ARTestResultSinkV0*, ARTestErrorBuffer*);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestCompilePlanFn)(ARTestEngineHandle, const ARTestPayloadView*, ARTestCompiledPlanHandle*, ARTestErrorBuffer*);
typedef void (ARTEST_ABI_CALL *ARTestDestroyCompiledPlanFn)(ARTestCompiledPlanHandle);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestSubscribeEventsFn)(ARTestEngineHandle, ARTestEngineEventFn, void*, ARTestSubscriptionHandle*, ARTestErrorBuffer*);
typedef void (ARTEST_ABI_CALL *ARTestUnsubscribeEventsFn)(ARTestEngineHandle, ARTestSubscriptionHandle);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestStartSessionFn)(ARTestEngineHandle, ARTestCompiledPlanHandle, ARTestSessionHandle*, ARTestErrorBuffer*);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestCancelSessionFn)(ARTestSessionHandle, ARTestErrorBuffer*);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestWaitSessionFn)(ARTestSessionHandle, uint32_t, ARTestBool32*, ARTestErrorBuffer*);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestGetSessionStateFn)(ARTestSessionHandle, ARTestSessionState*, ARTestErrorBuffer*);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestGetSessionResultFn)(ARTestSessionHandle, ARTestResultHandle*, ARTestErrorBuffer*);
typedef void (ARTEST_ABI_CALL *ARTestDestroySessionFn)(ARTestSessionHandle);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestSerializeResultFn)(ARTestResultHandle, const ARTestResultSinkV0*, ARTestErrorBuffer*);
typedef void (ARTEST_ABI_CALL *ARTestDestroyResultFn)(ARTestResultHandle);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestCompilePlanDetailedFn)(ARTestEngineHandle, const ARTestPayloadView*, ARTestCompiledPlanHandle*, const ARTestResultSinkV0*, ARTestErrorBuffer*);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestStartSessionControlledFn)(ARTestEngineHandle, ARTestCompiledPlanHandle, const ARTestSessionOptionsV0*, ARTestSessionHandle*, ARTestErrorBuffer*);
typedef ARTestStatus (ARTEST_ABI_CALL *ARTestValidateCatalogFn)(ARTestEngineHandle, ARTestStringView, const ARTestResultSinkV0*, ARTestErrorBuffer*);

typedef struct ARTestEngineApiV0
{
    uint32_t struct_size;
    uint32_t api_major;
    uint32_t api_minor;
    uint32_t reserved;
    ARTestCreateEngineFn create_engine;
    ARTestDestroyEngineFn destroy_engine;
    ARTestRefreshCatalogFn refresh_catalog;
    ARTestGetCatalogSnapshotFn get_catalog_snapshot;
    ARTestCompilePlanFn compile_plan;
    ARTestDestroyCompiledPlanFn destroy_compiled_plan;
    ARTestSubscribeEventsFn subscribe_events;
    ARTestUnsubscribeEventsFn unsubscribe_events;
    ARTestStartSessionFn start_session;
    ARTestCancelSessionFn cancel_session;
    ARTestWaitSessionFn wait_session;
    ARTestGetSessionStateFn get_session_state;
    ARTestGetSessionResultFn get_session_result;
    ARTestDestroySessionFn destroy_session;
    ARTestSerializeResultFn serialize_result;
    ARTestDestroyResultFn destroy_result;
    /* Added in API 0.2. Callers requesting API 0.1 provide only 144 bytes. */
    ARTestCompilePlanDetailedFn compile_plan_detailed;
    ARTestStartSessionControlledFn start_session_controlled;
    /* Added in API 0.3. Validation discovers metadata but never loads extension code. */
    ARTestValidateCatalogFn validate_catalog;
    /* Added in API 0.4. Bind a metadata snapshot without loading DLLs. */
    ARTestRefreshCatalogFn prepare_catalog;
} ARTestEngineApiV0;
#pragma pack(pop)

typedef ARTestStatus (ARTEST_ABI_CALL *ARTestEngineQueryApiFn)(uint32_t, uint32_t, ARTestEngineApiV0*, ARTestErrorBuffer*);

#ifdef __cplusplus
extern "C" {
#endif
ARTEST_ENGINE_EXPORT ARTestStatus ARTEST_ABI_CALL ARTestEngine_QueryApi(uint32_t, uint32_t, ARTestEngineApiV0*, ARTestErrorBuffer*);
#ifdef __cplusplus
}
#endif

#if defined(__cplusplus) && UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(ARTestStepExecutionInfoV0) == 40);
static_assert(sizeof(ARTestSessionOptionsV0) == 24);
static_assert(sizeof(ARTestEngineApiV0) == 176);
#endif

#endif

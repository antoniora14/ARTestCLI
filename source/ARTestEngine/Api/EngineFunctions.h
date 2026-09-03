#pragma once
#include "../../ARTest.SDK/include/ARTestEngineApi.h"
#include <cstdint>

namespace artest::engine
{
ARTestStatus ARTEST_ABI_CALL CreateEngine(const ARTestPayloadView *configuration,
                                          ARTestEngineHandle *output, ARTestErrorBuffer *error);
void ARTEST_ABI_CALL DestroyEngine(ARTestEngineHandle engine);
ARTestStatus ARTEST_ABI_CALL RefreshCatalog(ARTestEngineHandle engine,
                                            ARTestStringView approvedRoot,
                                            ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL ValidateCatalog(ARTestEngineHandle engine,
                                             ARTestStringView approvedRoot,
                                             const ARTestResultSinkV0 *sink,
                                             ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL PrepareCatalog(ARTestEngineHandle engine, ARTestStringView root,
                                            ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL GetCatalogSnapshot(ARTestEngineHandle engine,
                                                const ARTestResultSinkV0 *sink,
                                                ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL CompilePlan(ARTestEngineHandle engine,
                                         const ARTestPayloadView *payload,
                                         ARTestCompiledPlanHandle *output,
                                         ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL CompilePlanDetailed(ARTestEngineHandle engine,
                                                 const ARTestPayloadView *payload,
                                                 ARTestCompiledPlanHandle *output,
                                                 const ARTestResultSinkV0 *reportSink,
                                                 ARTestErrorBuffer *error);
void ARTEST_ABI_CALL DestroyCompiledPlan(ARTestCompiledPlanHandle plan);
ARTestStatus ARTEST_ABI_CALL SubscribeEvents(ARTestEngineHandle engine,
                                             ARTestEngineEventFn callback, void *context,
                                             ARTestSubscriptionHandle *output,
                                             ARTestErrorBuffer *error);
void ARTEST_ABI_CALL UnsubscribeEvents(ARTestEngineHandle engine,
                                       ARTestSubscriptionHandle subscription);
ARTestStatus ARTEST_ABI_CALL StartSession(ARTestEngineHandle engine, ARTestCompiledPlanHandle plan,
                                          ARTestSessionHandle *output, ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL StartSessionControlled(ARTestEngineHandle engine,
                                                    ARTestCompiledPlanHandle plan,
                                                    const ARTestSessionOptionsV0 *options,
                                                    ARTestSessionHandle *output,
                                                    ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL CancelSession(ARTestSessionHandle session, ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL WaitSession(ARTestSessionHandle session, std::uint32_t timeoutMs,
                                         ARTestBool32 *completed, ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL GetSessionState(ARTestSessionHandle session, ARTestSessionState *state,
                                             ARTestErrorBuffer *error);
ARTestStatus ARTEST_ABI_CALL GetSessionResult(ARTestSessionHandle session,
                                              ARTestResultHandle *output, ARTestErrorBuffer *error);
void ARTEST_ABI_CALL DestroySession(ARTestSessionHandle session);
ARTestStatus ARTEST_ABI_CALL SerializeRunResult(ARTestResultHandle result,
                                                const ARTestResultSinkV0 *sink,
                                                ARTestErrorBuffer *error);
void ARTEST_ABI_CALL DestroyRunResult(ARTestResultHandle result);
} // namespace artest::engine

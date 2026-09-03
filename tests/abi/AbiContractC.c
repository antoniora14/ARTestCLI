#include "../../source/ARTest.SDK/include/ARTestEngineApi.h"

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(ARTestStringView) == 16, "Unexpected string-view layout.");
_Static_assert(sizeof(ARTestExtensionApiV0) == 104, "Unexpected extension API layout.");
_Static_assert(sizeof(ARTestStepExecutionInfoV0) == 40, "Unexpected step-info layout.");
_Static_assert(sizeof(ARTestSessionOptionsV0) == 24, "Unexpected session-options layout.");
_Static_assert(sizeof(ARTestEngineApiV0) == 168, "Unexpected Engine API layout.");
#endif

int ARTestAbi_CLayoutFingerprint(void)
{
    return (int)(sizeof(ARTestHostApiV0)
        + sizeof(ARTestExtensionApiV0)
        + sizeof(ARTestEngineApiV0));
}

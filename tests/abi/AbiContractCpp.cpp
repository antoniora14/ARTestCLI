#include "../../source/ARTest.SDK/include/ARTestEngineApi.h"

extern "C" int ARTestAbi_CLayoutFingerprint(void);

static_assert(sizeof(ARTestStatus) == 4);
static_assert(sizeof(ARTestStepExecutionInfoV0) == 40);
static_assert(sizeof(ARTestSessionOptionsV0) == 24);
static_assert(sizeof(ARTestEngineApiV0) == 176);

int main()
{
    return ARTestAbi_CLayoutFingerprint() == 344 ? 0 : 1;
}

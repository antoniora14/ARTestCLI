#include "../../source/ARTest.SDK/include/ARTestEngineApi.h"

extern "C" int ARTestAbi_CLayoutFingerprint(void);

static_assert(sizeof(ARTestStatus) == 4);
static_assert(sizeof(ARTestEngineApiV0) == 144);

int main()
{
    return ARTestAbi_CLayoutFingerprint() == 312 ? 0 : 1;
}

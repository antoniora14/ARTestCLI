#pragma once
#include "Inspection.h"

namespace ARTestDev {
// Private development layout, not the public DEV-01.7 package contract.
Kit installedSdk();
Kit inspectStagingExecutable(const QString &executable);
}

#include "Version.h"

// BuildInfo.h is generated into the build tree by the pre-build step
// (cmake/increment_build.cmake). Isolating its use in this tiny TU means
// only Version.cpp recompiles when the build number changes.
#include "BuildInfo.h"

namespace m2suite {

int buildNumber() {
    return M2SUITE_BUILD_NUMBER;
}

const char* buildDate() {
    return M2SUITE_BUILD_DATE;
}

} // namespace m2suite

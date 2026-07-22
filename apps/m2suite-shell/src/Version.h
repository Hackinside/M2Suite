#pragma once

namespace m2suite {

// Auto-incrementing build number, bumped by cmake/increment_build.cmake on
// every compile and surfaced in the About dialog.
int buildNumber();
const char* buildDate();

} // namespace m2suite

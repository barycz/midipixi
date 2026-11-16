#include "mpx_test.h"

#include <cstring>

#include "midipixi/version.h"

TEST_CASE("version string is set") {
    CHECK(std::strlen(mpx::versionString()) > 0);
}

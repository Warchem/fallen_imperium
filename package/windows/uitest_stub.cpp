// (0.59.40) The Windows .exe is built without the screen-test mode (src/sim/uitest.cpp, developer only: the cloud build
// machine's 6 GB can't compile that 45,000-line file with clang). The game itself is the same.
#include <cstdio>
#include <string>
int runUiTest(const std::string&) {
    std::fprintf(stderr, "--uitest is not part of the Windows build (use the Linux build)\n");
    return 1;
}

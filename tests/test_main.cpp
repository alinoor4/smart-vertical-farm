#include <cstring>

#include "check.h"
#include "version.h"

// Proves the harness, CTest and the farm_core link all work.
TEST(smoke_version) {
    CHECK(farmVersion() != nullptr);
    CHECK(std::strlen(farmVersion()) > 0);
}

// Runs every test whose name contains argv[1] (all when absent).
int main(int argc, char** argv) {
    int ran = 0;
    for (const TestCase& test : allTests()) {
        if (argc > 1 && !std::strstr(test.name, argv[1])) continue;
        test.run();
        ++ran;
    }
    std::printf("%d test(s) run, %d check(s) failed\n", ran, failureCount());
    return failureCount() ? 1 : 0;
}

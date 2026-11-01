#include "TestFramework.h"
#include <cstring>

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedCases = 0;
    for (const auto& c : ScavarTest::Registry())
    {
        if (filter && !std::strstr(c.Name, filter)) continue;
        const int before = ScavarTest::Failures();
        c.Fn();
        ++run;
        const bool ok = ScavarTest::Failures() == before;
        if (!ok) ++failedCases;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.Name);
    }
    std::printf("\n%d test case(s), %d failed, %d check failure(s)\n", run, failedCases, ScavarTest::Failures());
    return ScavarTest::Failures() == 0 ? 0 : 1;
}

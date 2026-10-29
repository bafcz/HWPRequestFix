#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "CPUGuards.hpp"

int main() {
    CPUIdentity valid;
    valid.basic = {0x20, 0, 0, 0};
    valid.extended.a = 0x80000008;
    strcpy(valid.brand, "13th Gen Intel(R) Core(TM) i9-13900HX");
    assert(matchesCPUProfile(valid));
    auto cpu = valid;
    strcpy(cpu.brand, " 13th Gen Intel(R) Core(TM) i9-13900HX   ");
    assert(matchesCPUProfile(cpu));
    for (const char *brand : {"13th Gen Intel(R) Core(TM) i9-13900K",
                             "13th Gen Intel(R) Core(TM) i7-13700HX",
                             "13th Gen Intel(R) Core(TM) i9-13900HX spoof", ""}) {
        cpu = valid;
        strcpy(cpu.brand, brand);
        assert(!matchesCPUProfile(cpu));
    }
    cpu = valid; cpu.extended.a = 0x80000003; assert(!matchesCPUProfile(cpu));
    cpu = valid; memset(cpu.brand, ' ', sizeof(cpu.brand)); assert(!matchesCPUProfile(cpu));
    cpu = valid; memset(cpu.brand, 'X', sizeof(cpu.brand)); assert(!matchesCPUProfile(cpu));
    auto live = readCPUIdentity();
    printf("Live CPUID: signature=0x%x, brand=%s\n", live.signature.a, live.brand);
    assert(matchesCPUProfile(live));
    puts("PASS: exact i9-13900HX brand guard, CPUID leaf bounds, malformed brands and live CPUID.");
}

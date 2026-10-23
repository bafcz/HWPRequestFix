#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "CPUGuards.hpp"

int main() {
    CPUIdentity valid;
    valid.basic = {0x20, 0x756e6547, 0x6c65746e, 0x49656e69};
    valid.extended.a = 0x80000008;
    valid.signature.a = 0x000b0671;
    valid.power.a = 1U << 7;
    strcpy(valid.brand, "13th Gen Intel(R) Core(TM) i9-13900HX");
    assert(matchesCPUProfile(valid));
    for (unsigned stepping = 0; stepping < 16; ++stepping) {
        auto cpu = valid;
        cpu.signature.a = 0x000b0670 | stepping;
        assert(matchesCPUProfile(cpu));
    }
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
    cpu = valid; cpu.basic.a = 5; assert(!matchesCPUProfile(cpu));
    cpu = valid; cpu.extended.a = 0x80000003; assert(!matchesCPUProfile(cpu));
    cpu = valid; cpu.basic.b ^= 1; assert(!matchesCPUProfile(cpu));
    cpu = valid; cpu.basic.c ^= 1; assert(!matchesCPUProfile(cpu));
    cpu = valid; cpu.basic.d ^= 1; assert(!matchesCPUProfile(cpu));
    cpu = valid; cpu.signature.a ^= 0x10; assert(!matchesCPUProfile(cpu));
    cpu = valid; cpu.power.a = 0; assert(!matchesCPUProfile(cpu));
    cpu = valid; memset(cpu.brand, ' ', sizeof(cpu.brand)); assert(!matchesCPUProfile(cpu));
    cpu = valid; memset(cpu.brand, 'X', sizeof(cpu.brand)); assert(!matchesCPUProfile(cpu));
    auto live = readCPUIdentity();
    printf("Live CPUID: signature=0x%x, brand=%s\n", live.signature.a, live.brand);
    assert(matchesCPUProfile(live));
    puts("PASS: SKU/model/vendor/HWP/leaf guards, whitespace, malformed brands and live CPUID.");
}

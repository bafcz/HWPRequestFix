#pragma once
#include <stdint.h>

struct CPUIDResult { uint32_t a, b, c, d; };

inline CPUIDResult readCPUID(uint32_t leaf) {
    CPUIDResult r;
    __asm__ volatile("cpuid" : "=a"(r.a), "=b"(r.b), "=c"(r.c), "=d"(r.d)
                     : "a"(leaf), "c"(0));
    return r;
}

struct CPUIdentity {
    CPUIDResult basic {}, signature {}, power {}, extended {};
    char brand[49] {};
};

inline CPUIdentity readCPUIdentity() {
    CPUIdentity cpu;
    cpu.basic = readCPUID(0);
    cpu.extended = readCPUID(0x80000000);
    if (cpu.basic.a >= 1) cpu.signature = readCPUID(1);
    if (cpu.basic.a >= 6) cpu.power = readCPUID(6);
    if (cpu.extended.a >= 0x80000004) {
        for (unsigned i = 0; i < 3; ++i) {
            auto r = readCPUID(0x80000002 + i);
            const uint32_t words[] = {r.a, r.b, r.c, r.d};
            for (unsigned j = 0; j < 16; ++j)
                cpu.brand[i * 16 + j] = static_cast<char>(words[j / 4] >> (8 * (j % 4)));
        }
    }
    return cpu;
}

inline bool matchesCPUProfile(const CPUIdentity &cpu) {
    if (cpu.basic.a < 6 || cpu.extended.a < 0x80000004 ||
        cpu.basic.b != 0x756e6547 || cpu.basic.d != 0x49656e69 ||
        cpu.basic.c != 0x6c65746e ||
        (cpu.signature.a & 0x0fff0ff0) != 0x000b0670 ||
        (cpu.power.a & (1U << 7)) == 0) return false;

    // The 54 -> 69 endpoint was validated on this SKU, not every model B7 CPU.
    constexpr char expected[] = "13th Gen Intel(R) Core(TM) i9-13900HX";
    unsigned offset = 0;
    while (offset < 48 && cpu.brand[offset] == ' ') ++offset;
    for (unsigned i = 0; i < sizeof(expected) - 1; ++i) {
        if (offset >= 48 || cpu.brand[offset++] != expected[i]) return false;
    }
    while (offset < 48 && cpu.brand[offset] == ' ') ++offset;
    return cpu.brand[offset] == '\0';
}

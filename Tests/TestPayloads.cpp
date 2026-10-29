#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <sys/mman.h>
#include "Payloads.hpp"

static const uint8_t generalDisplaced[] = {
    0x0f, 0xb6, 0xf0, 0xc1, 0xe6, 0x18, 0x48, 0x89,
    0xd0, 0x48, 0xc1, 0xe0, 0x08, 0x09, 0xde,
};
static const uint8_t coreDisplaced[] = {
    0x45, 0x09, 0xc1, 0x49, 0x09, 0xc1, 0x41, 0xc1,
    0xe2, 0x18, 0x48, 0x09, 0xcb, 0x83, 0xfe, 0x01,
};

struct Registers { uint64_t rax, rbx, rcx, rdx, rsi, rdi, r8, r9, r10, r11, flags; };
extern "C" void runPayload(Registers *, void *);

unsigned scaledDesired(unsigned desired) {
    return (desired * 100000U + 78740U) / 78741U;
}

void testLinuxRaptorLakeMapping() {
    unsigned previous = 0;
    for (unsigned ratio = 0; ratio <= 54; ++ratio) {
        const unsigned compact = (ratio * 325U + 254U) >> 8;
        const unsigned linuxValue = scaledDesired(ratio);
        assert(compact == linuxValue);
        assert(ratio == 0 || compact >= previous);
        previous = compact;
    }
    assert(scaledDesired(0) == 0);
    assert(scaledDesired(42) == 54);
    assert(scaledDesired(54) == 69);
}

void *code(const uint8_t *payload, size_t length, const uint8_t *displaced,
           size_t originalLength) {
    auto p = static_cast<uint8_t *>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                                        MAP_ANON | MAP_PRIVATE, -1, 0));
    assert(p != MAP_FAILED);
    memcpy(p, payload, length);
    memcpy(p + length, displaced, originalLength);
    p[length + originalLength] = 0xc3;
    assert(mprotect(p, 4096, PROT_READ | PROT_EXEC) == 0);
    return p;
}

void compare(Registers actual, Registers expected, void *patched, void *original) {
    runPayload(&actual, patched);
    runPayload(&expected, original);
    // AF and OF are undefined after multi-bit shifts in the displaced code.
    actual.flags &= 0xc5;
    expected.flags &= 0xc5;
    if (memcmp(&actual, &expected, sizeof(actual)) != 0) {
        assert(false && "patched payload differs from displaced kernel instructions");
    }
}

void testPayloadBuilder(const HWPControl &control) {
    const uint8_t source[24] {};
    uint8_t out[24];
    uint8_t tiny[7];
    memset(out, 0xa5, sizeof(out));
    memset(tiny, 0xa5, sizeof(tiny));
    assert(makePayload(tiny, source, source + 24, source + 8, &control) == 0);
    assert(makePayload(out, source, source + 24, source + 17, &control) == 0);
    assert(makePayload(out, source + 1, source + 24, source, &control) == 0);
    assert(makePayload(out, source + 24, source, source, &control) == 0);
    assert(makePayload(out, source, source, source, &control) == 0);
    assert(makePayload(out, nullptr, source + 24, source + 8, &control) == 0);
    assert(makePayload(out, source, nullptr, source + 8, &control) == 0);
    assert(makePayload(out, source, source + 24, nullptr, &control) == 0);
    assert(makePayload(out, source, source + 24, source + 8, nullptr) == 0);
    for (auto b : out) assert(b == 0xa5);
    for (auto b : tiny) assert(b == 0xa5);
    for (unsigned offset : {0U, 16U}) {
        assert(makePayload(out, source, source + 24, source + offset, &control) == 24);
        uintptr_t pointer = 0;
        memcpy(&pointer, out + offset, sizeof(pointer));
        assert(pointer == reinterpret_cast<uintptr_t>(&control));
        for (unsigned i = 0; i < 24; ++i)
            if (i < offset || i >= offset + sizeof(pointer)) assert(out[i] == source[i]);
    }
}

uint64_t randomValue() {
    static uint64_t state = 0xd1b54a32d192ed03ULL;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

void check(Registers input, uint8_t gate, unsigned maximum, unsigned desired,
           unsigned minimum, unsigned epp, void *general, void *generalOriginal,
           void *core, void *coreOriginal) {
    input.rax = (input.rax & ~0xffULL) | epp;
    input.rbx = desired << 16;
    input.rcx = minimum;
    input.rdx = maximum;
    auto expected = input;
    if ((gate == 1 || gate == 2 || gate == 3 || gate == 6) && maximum == 54) {
        expected.rdx = gate == 6 ? 39 : 69;
        if (gate == 1 && desired == 54) expected.rbx = 69 << 16;
        if (gate == 2 && desired <= 54) expected.rbx = 0;
        if (gate == 3 && desired <= 54) expected.rbx = scaledDesired(desired) << 16;
        if (gate == 6 && desired <= 54) {
            expected.rbx = (desired > 39 ? 39 : desired) << 16;
        }
    }
    compare(input, expected, general, generalOriginal);
    const bool forceE = gate == 5;
    const bool forceP = gate == 1 || gate == 2 || gate == 3 || gate == 4;
    input.rax = ((forceP && maximum == 54) ? 69 : maximum) << 8;
    input.r9 = (desired << 16) | minimum;
    input.r8 = epp << 24;
    expected = input;
    if (forceE) {
        if (((expected.rax >> 8) & 255) > 39)
            expected.rax = (expected.rax & ~0xff00ULL) | 0x2700ULL;
        if (gate == 2 && desired <= 54) {
            expected.r9 = minimum;
        } else if (desired > 39) {
            expected.r9 = (39U << 16) | minimum;
        }
    } else if (forceP) {
        if (input.rax == 0x4500) {
            if (gate == 1 && desired == 54)
                expected.r9 = (69U << 16) | minimum;
            else if (gate == 2 && desired <= 54)
                expected.r9 = minimum;
            else if ((gate == 3 || gate == 4) && desired <= 54)
                expected.r9 = (scaledDesired(desired) << 16) | minimum;
        }
    } else {
        return;
    }
    compare(input, expected, core, coreOriginal);
}

int main() {
    testLinuxRaptorLakeMapping();
    HWPControl control {0, transformGeneralRequest, transformCoreRequest};
    testPayloadBuilder(control);
    uint8_t payload[512];
    size_t n = makePayload(payload, hwpGeneralStart, hwpGeneralEnd, hwpGeneralGate, &control);
    assert(n > 0 && n <= sizeof(payload));
    void *general = code(payload, n, generalDisplaced, sizeof(generalDisplaced));
    void *generalOriginal = code(payload, 0, generalDisplaced, sizeof(generalDisplaced));
    n = makePayload(payload, hwpCoreStart, hwpCoreEnd, hwpCoreGate, &control);
    assert(n > 0 && n <= sizeof(payload));
    void *core = code(payload, n, coreDisplaced, sizeof(coreDisplaced));
    void *coreOriginal = code(payload, 0, coreDisplaced, sizeof(coreDisplaced));
    unsigned long checks = 0;
    for (unsigned gate : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 255U}) {
        control.mode = static_cast<uint8_t>(gate);
        for (unsigned desired = 0; desired < 256; ++desired) {
            for (unsigned maximum = 0; maximum < 256; ++maximum) {
                for (unsigned epp : {0U, 32U, 128U, 255U}) {
                    for (unsigned minimum : {0U, 1U, 4U, 54U, 255U}) {
                        Registers input {0xaabbccddeeff0000ULL, 0, 0, 0,
                                         0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                                         0x1122334455667788ULL, 0, 0x8877665544332211ULL,
                                         0xaabbccddeeff0011ULL, 0x202 | ((desired << 3) & 0x8d5)};
                        check(input, gate, maximum, desired, minimum, epp,
                              general, generalOriginal, core, coreOriginal);
                        checks += 2;
                    }
                }
            }
        }
    }
    for (unsigned i = 0; i < 100000; ++i) {
        Registers input {randomValue(), 0, 0, 0, randomValue(), randomValue(),
                         randomValue(), 0, randomValue(), randomValue(),
                         0x202 | (randomValue() & 0x8d5)};
        const uint8_t gates[] = {0, 1, 2, 3, 4, 5, 6, 255};
        control.mode = gates[i % (sizeof(gates) / sizeof(gates[0]))];
        const unsigned maximum = i % 3 == 0 ? 54 : i % 3 == 1 ? 69 : randomValue() & 255;
        const unsigned desired = i % 2 == 0 ? 54 : randomValue() & 255;
        const unsigned minimum = i & 255;
        const unsigned epp = randomValue() & 255;
        check(input, control.mode, maximum, desired, minimum, epp,
              general, generalOriginal, core, coreOriginal);
        checks += 2;
    }
    printf("PASS: %lu C++ policy/assembly bridge comparisons; min/EPP boundaries, randomized registers/flags.\n", checks);
    puts("PASS: bounded payload construction; rejected layouts leave output untouched.");
    munmap(general, 4096); munmap(generalOriginal, 4096);
    munmap(core, 4096); munmap(coreOriginal, 4096);
}

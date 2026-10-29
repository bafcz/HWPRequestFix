#pragma once
#include <stddef.h>
#include <stdint.h>

struct HWPRegisterFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax, rflags;
};

struct HWPControl {
    volatile uint8_t mode;
    void (*general)(HWPRegisterFrame *, uint8_t, uint8_t);
    void (*core)(HWPRegisterFrame *, uint8_t, uint8_t);
};

static_assert(sizeof(HWPRegisterFrame) == 16 * sizeof(uint64_t), "Unexpected saved register frame");
static_assert(offsetof(HWPControl, general) == 8 && offsetof(HWPControl, core) == 16,
              "Assembly control-block offsets changed");

extern "C" void transformGeneralRequest(HWPRegisterFrame *frame, uint8_t mode,
                                         uint8_t highestPerformance);
extern "C" void transformCoreRequest(HWPRegisterFrame *frame, uint8_t mode,
                                      uint8_t highestPerformance);

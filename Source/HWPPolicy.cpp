#include "HWPPolicy.hpp"

namespace {
uint32_t replaceByte(uint32_t word, unsigned shift, uint8_t value) {
    const uint32_t mask = static_cast<uint32_t>(0xff) << shift;
    return (word & ~mask) | (static_cast<uint32_t>(value) << shift);
}

uint8_t scalePcoreRequest(uint8_t ratio) {
    return static_cast<uint8_t>((static_cast<uint32_t>(ratio) * 325U + 254U) >> 8);
}

}

extern "C" void transformGeneralRequest(HWPRegisterFrame *frame, uint8_t mode,
                                         uint8_t highestPerformance) {
    if (!frame || (mode != 1 && mode != 2 && mode != 3
#ifdef HWP_PAYLOAD_TEST
                   && mode != 6
#endif
                   )) return;

    if (static_cast<uint32_t>(frame->rdx) != 54) return;

    if (mode == 1) {
        frame->rdx = 69;
        if (static_cast<uint32_t>(frame->rbx) == 0x00360000U)
            frame->rbx = (frame->rbx & ~uint64_t(0x00ff0000)) | 0x00450000U;
        return;
    }

    if (mode == 2) {
        frame->rdx = 69;
        if (static_cast<uint32_t>(frame->rbx) <= 0x00360000U) frame->rbx = 0;
        return;
    }

    const bool eCore = highestPerformance == 39;
    const uint16_t desired = static_cast<uint16_t>(frame->rbx >> 16);
    if (eCore) {
        frame->rdx = 39;
        if (desired <= 54)
            frame->rbx = replaceByte(static_cast<uint32_t>(frame->rbx), 16,
                                     desired > 39 ? 39 : static_cast<uint8_t>(desired));
    } else {
        frame->rdx = 69;
        if (desired <= 54)
            frame->rbx = replaceByte(static_cast<uint32_t>(frame->rbx), 16,
                                     scalePcoreRequest(static_cast<uint8_t>(desired)));
    }
}

extern "C" void transformCoreRequest(HWPRegisterFrame *frame, uint8_t mode,
                                      uint8_t highestPerformance) {
    if (!frame || (mode != 1 && mode != 2 && mode != 3
#ifdef HWP_PAYLOAD_TEST
                   && mode != 4 && mode != 5
#endif
                   )) return;

    const uint8_t highest = highestPerformance;
    const uint64_t packedMaximum = frame->rax;
    const uint8_t maximum = static_cast<uint8_t>(packedMaximum >> 8);
    const uint8_t desired = static_cast<uint8_t>(frame->r9 >> 16);
    const uint16_t shiftedDesired = static_cast<uint16_t>(frame->r9 >> 16);

    if (highest == 39) {
        if (maximum > 39)
            frame->rax = (packedMaximum & ~uint64_t(0xff00)) | 0x2700;
        if (mode == 2) {
            if (desired <= 54)
                frame->r9 = static_cast<uint32_t>(frame->r9) & 0xff00ffffU;
        } else if (desired > 39) {
            frame->r9 = replaceByte(static_cast<uint32_t>(frame->r9), 16, 39);
        }
        return;
    }

    if (packedMaximum != 0x4500) return;
    if (mode == 1) {
        if (shiftedDesired == 54)
            frame->r9 = replaceByte(static_cast<uint32_t>(frame->r9), 16, 69);
    } else if (mode == 2) {
        if (shiftedDesired <= 54)
            frame->r9 = static_cast<uint32_t>(frame->r9) & 0xff00ffffU;
    } else if (shiftedDesired <= 54) {
        frame->r9 = replaceByte(static_cast<uint32_t>(frame->r9), 16,
                                scalePcoreRequest(static_cast<uint8_t>(shiftedDesired)));
    }
}

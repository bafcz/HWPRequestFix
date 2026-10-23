#pragma once
#include <stdint.h>
#include <stddef.h>

extern "C" {
extern const uint8_t hwpGeneralStart[], hwpGeneralGate[], hwpGeneralEnd[];
extern const uint8_t hwpCoreStart[], hwpCoreGate[], hwpCoreEnd[];
}

template <size_t Capacity>
inline size_t makePayload(uint8_t (&out)[Capacity], const uint8_t *start,
                          const uint8_t *end, const uint8_t *gate,
                          const volatile uint8_t *enabled) {
    const auto begin = reinterpret_cast<uintptr_t>(start);
    const auto finish = reinterpret_cast<uintptr_t>(end);
    const auto slot = reinterpret_cast<uintptr_t>(gate);
    if (!start || !end || !gate || !enabled || finish <= begin || slot < begin ||
        slot > finish || finish - begin > Capacity || finish - slot < sizeof(uintptr_t)) return 0;
    size_t size = finish - begin;
    for (size_t i = 0; i < size; ++i) out[i] = start[i];
    uintptr_t pointer = reinterpret_cast<uintptr_t>(enabled);
    size_t gateOffset = slot - begin;
    for (size_t i = 0; i < sizeof(pointer); ++i)
        out[gateOffset + i] = static_cast<uint8_t>(pointer >> (8 * i));
    return size;
}

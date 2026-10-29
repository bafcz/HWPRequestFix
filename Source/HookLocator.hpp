#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <mach-o/loader.h>
#include <mach/vm_prot.h>

namespace HWPHookLocator {
static constexpr uint8_t generalPattern[] = {
    0x0f, 0xb6, 0xf0, 0xc1, 0xe6, 0x18, 0x48, 0x89,
    0xd0, 0x48, 0xc1, 0xe0, 0x08, 0x09, 0xde,
};
static constexpr uint8_t corePattern[] = {
    0x45, 0x09, 0xc1, 0x49, 0x09, 0xc1, 0x41, 0xc1,
    0xe2, 0x18, 0x48, 0x09, 0xcb, 0x83, 0xfe, 0x01,
};
static constexpr size_t pairDistance = 0x68;
static constexpr size_t maximumSearch = 0x10000;

inline bool findTextRange(const mach_header_64 *header, uintptr_t target,
                          uintptr_t &textStart, uintptr_t &textEnd) {
    if (!header || header->magic != MH_MAGIC_64 ||
        header->sizeofcmds > 0x10000) return false;
    const auto headerAddress = reinterpret_cast<uintptr_t>(header);
    if (headerAddress > UINTPTR_MAX - sizeof(*header) - header->sizeofcmds) return false;
    const auto *cursor = reinterpret_cast<const uint8_t *>(header + 1);
    const auto *commandsEnd = cursor + header->sizeofcmds;
    uintptr_t slide = 0;
    bool foundTextSegment = false;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (cursor > commandsEnd || static_cast<size_t>(commandsEnd - cursor) < sizeof(load_command))
            return false;
        const auto *command = reinterpret_cast<const load_command *>(cursor);
        if (command->cmdsize < sizeof(load_command) ||
            command->cmdsize > static_cast<size_t>(commandsEnd - cursor)) return false;
        if (command->cmd == LC_SEGMENT_64 && command->cmdsize >= sizeof(segment_command_64)) {
            const auto *segment = reinterpret_cast<const segment_command_64 *>(command);
            if (!memcmp(segment->segname, "__TEXT", 6)) {
                if (headerAddress < segment->vmaddr) return false;
                slide = headerAddress - segment->vmaddr;
                foundTextSegment = true;
            }
        }
        cursor += command->cmdsize;
    }
    if (!foundTextSegment || cursor != commandsEnd) return false;

    cursor = reinterpret_cast<const uint8_t *>(header + 1);
    size_t matches = 0;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const auto *command = reinterpret_cast<const load_command *>(cursor);
        if (command->cmd == LC_SEGMENT_64) {
            if (command->cmdsize < sizeof(segment_command_64)) return false;
            const auto *segment = reinterpret_cast<const segment_command_64 *>(command);
            const auto sectionBytes = static_cast<size_t>(segment->nsects) * sizeof(section_64);
            if (sizeof(*segment) + sectionBytes > command->cmdsize) return false;
            const auto *sections = reinterpret_cast<const section_64 *>(segment + 1);
            for (uint32_t j = 0; j < segment->nsects; ++j) {
                const auto &section = sections[j];
                if (memcmp(section.sectname, "__text", 6) ||
                    !(segment->initprot & VM_PROT_EXECUTE)) continue;
                if (section.addr > UINTPTR_MAX - slide || section.size > UINTPTR_MAX - (section.addr + slide))
                    return false;
                const uintptr_t start = section.addr + slide;
                const uintptr_t end = start + section.size;
                if (target >= start && target < end) {
                    textStart = start;
                    textEnd = end;
                    ++matches;
                }
            }
        }
        cursor += command->cmdsize;
    }
    return matches == 1;
}

inline bool matches(const uint8_t *bytes, size_t length, size_t offset,
                    const uint8_t *pattern, size_t patternLength) {
    if (offset > length || patternLength > length - offset) return false;
    for (size_t i = 0; i < patternLength; ++i)
        if (bytes[offset + i] != pattern[i]) return false;
    return true;
}

inline bool locate(const uint8_t *bytes, size_t length, size_t deadlineOffset,
                   size_t &generalOffset, size_t &coreOffset) {
    if (!bytes || deadlineOffset > length || deadlineOffset > maximumSearch) return false;
    size_t matchesFound = 0;
    for (size_t i = 0; i < deadlineOffset; ++i) {
        if (!matches(bytes, length, i, generalPattern, sizeof(generalPattern))) continue;
        if (i > deadlineOffset || pairDistance > deadlineOffset - i) continue;
        const size_t core = i + pairDistance;
        if (!matches(bytes, length, core, corePattern, sizeof(corePattern))) continue;
        generalOffset = i;
        coreOffset = core;
        if (++matchesFound > 1) return false;
    }
    return matchesFound == 1;
}
}

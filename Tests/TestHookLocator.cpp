#include <cassert>
#include <cstring>
#include "HookLocator.hpp"

int main() {
    uint8_t code[HWPHookLocator::maximumSearch] {};
    const size_t site = 0x3210;
    memcpy(code + site, HWPHookLocator::generalPattern,
           sizeof(HWPHookLocator::generalPattern));
    memcpy(code + site + HWPHookLocator::pairDistance,
           HWPHookLocator::corePattern, sizeof(HWPHookLocator::corePattern));

    size_t general = 0, core = 0;
    assert(HWPHookLocator::locate(code, sizeof(code), sizeof(code), general, core));
    assert(general == site);
    assert(core == site + HWPHookLocator::pairDistance);
    assert(!HWPHookLocator::locate(code, sizeof(code), site, general, core));

    memcpy(code + 0x7000, HWPHookLocator::generalPattern,
           sizeof(HWPHookLocator::generalPattern));
    memcpy(code + 0x7000 + HWPHookLocator::pairDistance,
           HWPHookLocator::corePattern, sizeof(HWPHookLocator::corePattern));
    assert(!HWPHookLocator::locate(code, sizeof(code), sizeof(code), general, core));

    alignas(16) uint8_t image[4096] {};
    auto *header = reinterpret_cast<mach_header_64 *>(image);
    header->magic = MH_MAGIC_64;
    header->ncmds = 1;
    header->sizeofcmds = sizeof(segment_command_64) + sizeof(section_64);
    auto *segment = reinterpret_cast<segment_command_64 *>(header + 1);
    segment->cmd = LC_SEGMENT_64;
    segment->cmdsize = static_cast<uint32_t>(header->sizeofcmds);
    memcpy(segment->segname, "__TEXT", 6);
    segment->vmaddr = reinterpret_cast<uintptr_t>(image);
    // Kernel Collection fileset entries have a non-zero file offset.
    segment->fileoff = 0xE8000;
    segment->initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    segment->nsects = 1;
    auto *section = reinterpret_cast<section_64 *>(segment + 1);
    memcpy(section->sectname, "__text", 6);
    section->addr = reinterpret_cast<uintptr_t>(image + 0x800);
    section->size = 0x400;
    uintptr_t textStart = 0, textEnd = 0;
    assert(HWPHookLocator::findTextRange(header,
        reinterpret_cast<uintptr_t>(image + 0x900), textStart, textEnd));
    assert(textStart == reinterpret_cast<uintptr_t>(image + 0x800));
    assert(textEnd == reinterpret_cast<uintptr_t>(image + 0xc00));
    assert(!HWPHookLocator::findTextRange(header,
        reinterpret_cast<uintptr_t>(image + 0x500), textStart, textEnd));
    return 0;
}

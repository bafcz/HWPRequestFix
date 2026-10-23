#pragma once
#include <mach-o/loader.h>

inline bool matchesKernelUUID(const void *image, unsigned long available,
                              const unsigned char (&expected)[16]) {
    if (!image || available < sizeof(mach_header_64)) return false;
    auto header = static_cast<const mach_header_64 *>(image);
    if (header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_X86_64 ||
        header->filetype != MH_EXECUTE ||
        header->sizeofcmds > available - sizeof(*header)) return false;
    auto bytes = static_cast<const unsigned char *>(image);
    unsigned long offset = sizeof(*header);
    const auto end = offset + header->sizeofcmds;
    bool found = false;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (end - offset < sizeof(load_command)) return false;
        auto command = reinterpret_cast<const load_command *>(bytes + offset);
        if (command->cmdsize < sizeof(*command) || command->cmdsize % 8 != 0 ||
            command->cmdsize > end - offset) return false;
        if (command->cmd == LC_UUID) {
            if (found || command->cmdsize != sizeof(uuid_command)) return false;
            auto uuid = reinterpret_cast<const uuid_command *>(command);
            for (unsigned j = 0; j < 16; ++j)
                if (uuid->uuid[j] != expected[j]) return false;
            found = true;
        }
        offset += command->cmdsize;
    }
    return found && offset == end;
}

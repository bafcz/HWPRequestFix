#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "KernelGuards.hpp"

int main() {
    alignas(8) unsigned char bytes[8192] {};
    auto h = reinterpret_cast<mach_header_64 *>(bytes);
    h->magic = MH_MAGIC_64;
    h->cputype = CPU_TYPE_X86_64;
    h->filetype = MH_EXECUTE;
    h->ncmds = 1;
    h->sizeofcmds = sizeof(uuid_command);
    auto u = reinterpret_cast<uuid_command *>(bytes + sizeof(*h));
    u->cmd = LC_UUID;
    u->cmdsize = sizeof(*u);
    const unsigned char expected[16] = {0x12, 0x91, 0xd4, 0x4a};
    memcpy(u->uuid, expected, 16);
    assert(matchesKernelUUID(bytes, sizeof(bytes), expected));
    assert(matchesKernelUUID(bytes, sizeof(*h) + sizeof(*u), expected));
    assert(!matchesKernelUUID(nullptr, sizeof(bytes), expected));
    assert(!matchesKernelUUID(bytes, 8, expected));
    assert(!matchesKernelUUID(bytes, sizeof(*h) + sizeof(*u) - 1, expected));
    h->magic ^= 1;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    h->magic ^= 1;
    h->cputype = CPU_TYPE_ARM64;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    h->cputype = CPU_TYPE_X86_64;
    h->filetype = MH_FILESET;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    h->filetype = MH_EXECUTE;
    h->ncmds = 0;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    h->ncmds = 0xffffffff;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    h->ncmds = 1;
    h->sizeofcmds = 7;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    h->sizeofcmds = sizeof(*u);
    u->cmd = 0x7ffffffe;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    u->cmd = LC_UUID;
    u->uuid[15] = 1;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    u->uuid[15] = 0;
    u->cmdsize = 0;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    u->cmdsize = sizeof(load_command);
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    u->cmdsize = sizeof(*u) - 1;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    u->cmdsize = sizeof(bytes);
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    u->cmdsize = sizeof(*u);
    h->sizeofcmds = sizeof(bytes);
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    h->sizeofcmds = sizeof(*u);
    h->ncmds = 2;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    h->sizeofcmds *= 2;
    memcpy(bytes + sizeof(*h) + sizeof(*u), u, sizeof(*u));
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    // Unknown well-formed commands are allowed; a UUID is still mandatory.
    auto second = reinterpret_cast<uuid_command *>(bytes + sizeof(*h) + sizeof(*u));
    u->cmd = 0x7ffffffe;
    assert(matchesKernelUUID(bytes, sizeof(bytes), expected));
    second->cmd = 0x7ffffffe;
    assert(!matchesKernelUUID(bytes, sizeof(bytes), expected));
    const char *paths[] = {"/System/Library/Kernels/kernel",
                          "/System/Library/KernelCollections/BootKernelExtensions.kc"};
    const unsigned char actual[16] = {0x12,0x91,0xd4,0x4a,0xc0,0xcd,0x3a,0xc7,
                                    0xa9,0xe4,0xca,0x27,0x49,0xb3,0x39,0x59};
    for (auto path : paths) {
        auto f = fopen(path, "rb");
        assert(f);
        fseek(f, 0, SEEK_END);
        std::vector<unsigned char> file(static_cast<size_t>(ftell(f)));
        rewind(f);
        assert(fread(file.data(), 1, file.size(), f) == file.size());
        fclose(f);
        size_t base = 0;
        if (reinterpret_cast<const mach_header_64 *>(file.data())->filetype == MH_FILESET) {
            auto outer = reinterpret_cast<const mach_header_64 *>(file.data());
            size_t offset = sizeof(*outer);
            bool found = false;
            for (uint32_t i = 0; i < outer->ncmds; ++i) {
                auto c = reinterpret_cast<const load_command *>(file.data() + offset);
                if (c->cmd == LC_FILESET_ENTRY) {
                    auto entry = reinterpret_cast<const fileset_entry_command *>(c);
                    const auto name = reinterpret_cast<const char *>(entry) + entry->entry_id.offset;
                    if (strcmp(name, "com.apple.kernel") == 0) { base = entry->fileoff; found = true; }
                }
                offset += c->cmdsize;
            }
            assert(found);
        }
        assert(matchesKernelUUID(file.data() + base, 8192, actual));
    }
    puts("PASS: immutable UUID guard, malformed-header rejection, standalone and BootKC kernel headers.");
}

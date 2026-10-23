# HWPRequestFix

Experimental Lilu plugin for the 13th Gen Intel Core i9-13900HX running the
validated Darwin 25.6.0 / 25G241 kernel. Version 0.1.12 was verified on one
Y9000P 2023 installation. This is not a general-purpose CPU power driver.

The plugin hooks two XCPM HWP request paths. In `scaled-intermediate` mode it
maps P-core desired values using the Raptor Lake conversion used by Linux
intel_pstate, translates the top maximum request from 54 to 69, and keeps E-core
maximum and desired values at or below 39. Minimum requests and EPP are
preserved. It does not poll, continuously write MSRs, alter turbo-ratio tables,
or change voltage or power limits. HWP request values are not MHz guarantees.

The implementation deliberately rejects CPUs, kernel builds, and kernel code
signatures that do not match its measured target. Updating macOS requires
revalidating the kernel signatures before building or loading the plugin.

## Build prerequisites

- macOS with Xcode command-line tools (`clang`, `ld`, `codesign`, `otool`, `nm`)
- MacKernelSDK, pointed to by `MAC_KERNEL_SDK`
- Lilu 1.7.2 source, with `LILU_SOURCE_DIR` pointing to its `Lilu` subdirectory
- The matching active Lilu binary, pointed to by `HWP_LILU_BINARY`
- The matching kernel and BootKC. Override their paths with `HWP_KERNEL` and
  `HWP_BOOTKC` if needed.

Example:

```sh
export MAC_KERNEL_SDK=/path/to/MacKernelSDK
export LILU_SOURCE_DIR=/path/to/Lilu/Lilu
export HWP_LILU_BINARY=/path/to/Lilu.kext/Contents/MacOS/Lilu
bash build.sh
```

`build.sh` runs offline CPU/payload/kernel-signature tests and builds the kext
under `Build/`. Building does not establish that the plugin is safe on a
different kernel or machine. EFI deployment is intentionally not included.

## Source map

- `Source/HWPRequestFix.cpp`: target guards, Lilu hooks, activation and diagnostics.
- `Source/Payloads.S`: the two request transformations.
- `Source/CPUGuards.hpp`, `KernelGuards.hpp`, `Payloads.hpp`: identity checks and
  bounded payload construction.
- `Tests/`: native payload and guard tests plus binary signature/import checks.

The plugin depends on Lilu 1.7.2. Lilu is an external dependency and is not
vendored into this repository.

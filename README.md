# HWPRequestFix

Experimental Lilu plugin for the 13th Gen Intel Core i9-13900HX. Version 0.2.2
refactors the verified 0.1.12 request transformation into C++ and leaves a small
assembly bridge to preserve the interrupted kernel register state. The exact
CPU brand check remains. Kernel version, UUID, symbol-distance, and fixed helper
offset checks have been replaced by bounded runtime discovery of the XCPM
request sites.

The locator resolves `_xcpm_get_deadline`, obtains Lilu's KC-aware running
kernel Mach-O header and reads the `__text` section bounds. It accepts the
non-zero file offsets used by Kernel Collection fileset entries, then searches at
most 64 KiB before that symbol for one unique
pair of exact request instruction sequences. It refuses to patch if the text
range is invalid or the pair is missing or ambiguous. This is structural
discovery, not a promise of compatibility with arbitrary macOS kernels; every
new build still requires offline inspection and boot/runtime validation.

The assembly bridge saves/restores all general-purpose registers and RFLAGS,
passes the saved frame to C++, then replays the displaced instructions. HWP
capability MSR 0x771 is read in the bridge on the executing logical CPU and
passed as data to the policy function. C++ is compiled without SSE/MMX so the
kernel callback cannot introduce vector-state clobbering. The policy preserves
MIN/EPP, translates the P-core desired value using the tested Raptor Lake
mapping, and caps E-core requests at 39. It does not poll, change voltage or
power limits, or set hardware clocks.

## Build

Prerequisites are macOS Xcode command-line tools, MacKernelSDK, Lilu 1.7.2
source, and the matching Lilu binary. Set `MAC_KERNEL_SDK`, `LILU_SOURCE_DIR`,
and `HWP_LILU_BINARY`, then run:

```sh
bash build.sh
```

`HWP_KERNEL` and `HWP_BOOTKC` may select the local kernel and BootKC used for
offline validation. The tests validate CPU identity, unique hook discovery,
Mach-O text bounds, register/flag preservation, policy transformations, and
imports. Build output is under `Build/`. EFI deployment is intentionally not
included.

## Source map

- `Source/HWPRequestFix.cpp`: 13900HX identity guard, runtime hook discovery,
  Lilu routing, activation and diagnostics.
- `Source/Payloads.S`: minimal register-save/restore bridge to policy code.
- `Source/HWPPolicy.cpp`: HWP request conversion and P/E capability handling.
- `Source/HookLocator.hpp`: bounded Mach-O text-section and request-site search.
- `Tests/`: offline CPU, hook-locator, and register/policy tests.

Version 0.2.2 passed the offline build/tests and was runtime-validated on the
Y9000P 2023 with macOS 26.0, Darwin 25.6.0, and an i9-13900HX. The runtime
state reported `both-hooks-installed` and `Active = Yes`. Keep a prior kext
copy as recovery when changing the EFI configuration.

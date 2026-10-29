#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
sdk=${MAC_KERNEL_SDK:?Set MAC_KERNEL_SDK to the MacKernelSDK path}
lilu=${LILU_SOURCE_DIR:?Set LILU_SOURCE_DIR to the Lilu source directory}
test -f "$lilu/Headers/kern_api.hpp"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' Info.plist)
test "$version" = "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' Info.plist)"
ruby Tests/VerifyKernel.rb
xcrun clang++ -arch x86_64 -std=c++14 -O2 -Wall -Wextra -Werror \
    -ISource Tests/TestCPUGuards.cpp -o Build/TestCPUGuards
Build/TestCPUGuards
xcrun clang++ -arch x86_64 -std=c++14 -O2 -Wall -Wextra -Werror \
    -ISource Tests/TestHookLocator.cpp -o Build/TestHookLocator
Build/TestHookLocator
xcrun clang -arch x86_64 -DHWP_PAYLOAD_TEST -c Source/Payloads.S -o Build/TestPayloadsPayloads.o
xcrun clang++ -arch x86_64 -std=c++14 -O2 -Wall -Wextra -Werror -DHWP_PAYLOAD_TEST \
    -mno-sse -mno-sse2 -mno-mmx -ISource -IBuild Tests/TestPayloads.cpp \
    Source/HWPPolicy.cpp Tests/Harness.S Build/TestPayloadsPayloads.o -o Build/TestPayloads
Build/TestPayloads
mkdir -p Build/HWPRequestFix.kext/Contents/MacOS
flags=(-arch x86_64 -mmacosx-version-min=10.8 -mkernel -fapple-kext -mno-red-zone
    -fno-exceptions -fno-rtti -fno-stack-protector -fno-common -fno-builtin -nostdinc \
    -mno-sse -mno-sse2 -mno-mmx
    -fvisibility=hidden -O2 -Wall -Wextra -Werror -Wno-unused-parameter
    -DKERNEL -DKERNEL_PRIVATE -D__KERNEL__ -D__APPLE_USE_RFC_3542
    -DPRODUCT_NAME=HWPRequestFix -DMODULE_VERSION="$version" -DLILU_CUSTOM_IOKIT_INIT=1
    -I"$sdk/Headers" -isystem "$lilu" -ISource -IBuild)
xcrun clang++ -std=c++14 "${flags[@]}" -c Source/HWPRequestFix.cpp -o Build/HWPRequestFix.o
xcrun clang++ -std=c++14 "${flags[@]}" -c Source/HWPPolicy.cpp -o Build/HWPPolicy.o
xcrun clang++ -std=c++14 "${flags[@]}" -c "$lilu/Library/plugin_start.cpp" -o Build/plugin_start.o
xcrun clang -arch x86_64 -mmacosx-version-min=10.8 -c Source/Payloads.S -o Build/Payloads.o
xcrun ld -arch x86_64 -kext -undefined dynamic_lookup \
    -o Build/HWPRequestFix.kext/Contents/MacOS/HWPRequestFix \
    Build/HWPRequestFix.o Build/HWPPolicy.o Build/plugin_start.o Build/Payloads.o \
    -L"$sdk/Library/x86_64" -lkmod
cp Info.plist Build/HWPRequestFix.kext/Contents/Info.plist
ruby Tests/VerifyImports.rb
codesign --force --sign - --timestamp=none Build/HWPRequestFix.kext
codesign --verify --strict Build/HWPRequestFix.kext
plutil -lint Build/HWPRequestFix.kext/Contents/Info.plist
file Build/HWPRequestFix.kext/Contents/MacOS/HWPRequestFix
echo 'Build and offline tests complete. Runtime/boot verification still required.'

#include <Headers/kern_api.hpp>
#include <Headers/plugin_start.hpp>
#include <mach/mach_types.h>
#include "Payloads.hpp"
#include "CPUGuards.hpp"
#include "HookLocator.hpp"

namespace {
HWPControl control {0, transformGeneralRequest, transformCoreRequest};
bool installed = false;
bool serviceReady = false;
const char *status = "not-installed";
IOService *service = nullptr;
CPUIdentity cpuIdentity;
bool cpuProfileMatched = false;
// Scale P-core desired requests; E-cores retain native 100 MHz units.
// Autonomous selection remains opt-in because it clears desired.
enum RequestMode : uint8_t {
    ModeLegacy = 1,
    ModeAutonomous = 2,
    ModeScaledIntermediate = 3,
};
uint8_t requestMode = ModeScaledIntermediate;

const char *requestModeName() {
    switch (requestMode) {
        case ModeLegacy: return "legacy-top-only";
        case ModeAutonomous: return "hardware-autonomous";
        case ModeScaledIntermediate: return "scaled-intermediate";
        default: return "disabled";
    }
}

void publish() {
    if (!service) return;
    service->setProperty("Active", control.mode != 0);
    service->setProperty("HookStatus", status);
    service->setProperty("CPUProfileMatched", cpuProfileMatched);
    service->setProperty("CPUBrand", cpuIdentity.brand);
    service->setProperty("RequestMode", requestModeName());
    service->setProperty("HookLocator", "unique-paired-instruction-signatures");
}

void setStatus(const char *value) {
    status = value;
    SYSLOG("hwp", "status: %s", value);
    publish();
}

void activate() {
    if (installed && serviceReady) {
        __atomic_store_n(&control.mode, requestMode, __ATOMIC_RELEASE);
        SYSLOG("hwp", "active: maximum 54 -> 69; desired mode %s; minimum/EPP unchanged",
               requestModeName());
    }
    publish();
}

bool findKernelTextRange(uintptr_t target, uintptr_t &textStart, uintptr_t &textEnd,
                         const char *&failureStatus) {
    auto kernelInfo = MachInfo::create(true);
    if (!kernelInfo) {
        failureStatus = "kernel-info-allocation-failed";
        return false;
    }

    bool found = false;
    if (kernelInfo->getRunningAddresses() == KERN_SUCCESS) {
        uint8_t *headerBytes = nullptr;
        size_t imageSize = 0;
        kernelInfo->getRunningPosition(headerBytes, imageSize);
        if (headerBytes && imageSize >= sizeof(mach_header_64)) {
            found = HWPHookLocator::findTextRange(
                reinterpret_cast<const mach_header_64 *>(headerBytes), target,
                textStart, textEnd);
            if (!found) failureStatus = "kernel-text-range-unavailable";
        } else {
            failureStatus = "running-kernel-header-unavailable";
        }
    } else {
        failureStatus = "running-kernel-image-unavailable";
    }

    kernelInfo->deinit();
    MachInfo::deleter(kernelInfo);
    return found;
}

void patchKernel(void *, KernelPatcher &patcher) {
    cpuIdentity = readCPUIdentity();
    cpuProfileMatched = matchesCPUProfile(cpuIdentity);
    if (!cpuProfileMatched) {
        setStatus("unsupported-cpu-profile");
        return;
    }
    auto deadline = patcher.solveSymbol(KernelPatcher::KernelID, "_xcpm_get_deadline");
    patcher.clearError();
    if (!deadline) { setStatus("missing-xcpm-get-deadline"); return; }
    uintptr_t textStart = 0, textEnd = 0;
    const auto deadlineAddress = static_cast<uintptr_t>(deadline);
    const char *rangeFailure = "kernel-text-range-unavailable";
    if (!findKernelTextRange(deadlineAddress, textStart, textEnd, rangeFailure) ||
        deadlineAddress > textEnd) {
        setStatus(rangeFailure);
        return;
    }
    const auto scanStart = deadlineAddress - textStart > HWPHookLocator::maximumSearch
        ? deadlineAddress - HWPHookLocator::maximumSearch : textStart;
    const size_t scanLength = deadlineAddress - scanStart;
    auto search = reinterpret_cast<const uint8_t *>(scanStart);
    size_t generalOffset = 0, coreOffset = 0;
    if (!HWPHookLocator::locate(search, scanLength, scanLength,
                                generalOffset, coreOffset)) {
        setStatus("request-hook-pair-not-unique");
        return;
    }
    auto generalSite = reinterpret_cast<mach_vm_address_t>(search + generalOffset);
    auto coreSite = reinterpret_cast<mach_vm_address_t>(search + coreOffset);
    // Lilu's entry trampoline must remain within rel32 reach for an atomic patch.
    auto distance = static_cast<int64_t>(reinterpret_cast<uintptr_t>(&lilu) - generalSite);
    if (distance <= -0x70000000LL || distance >= 0x70000000LL) {
        setStatus("lilu-outside-near-jump-range");
        return;
    }

    // Both wrappers remain inert until both installations and IOKit startup succeed.
    uint8_t general[384], core[384];
    auto n1 = makePayload(general, hwpGeneralStart, hwpGeneralEnd, hwpGeneralGate, &control);
    auto n2 = makePayload(core, hwpCoreStart, hwpCoreEnd, hwpCoreGate, &control);
    if (!n1 || !n2) { setStatus("invalid-payload-layout-inert"); return; }
    patcher.clearError();
    auto t1 = patcher.routeBlock(generalSite, general, n1, true);
    if (t1 == EINVAL || !t1 || patcher.getError() != KernelPatcher::Error::NoError) {
        patcher.clearError();
        setStatus("general-hook-failed-inert");
        return;
    }
    auto t2 = patcher.routeBlock(coreSite, core, n2, true);
    if (t2 == EINVAL || !t2 || patcher.getError() != KernelPatcher::Error::NoError) {
        patcher.clearError();
        setStatus("core-hook-failed-inert");
        SYSLOG("hwp", "second hook failed; shared enable flag remains off");
        return;
    }
    installed = true;
    setStatus("both-hooks-installed");
    SYSLOG("hwp", "both request hooks installed; P-state tables untouched");
    activate();
}

void pluginStart() {
    requestMode = ModeScaledIntermediate;
    if (checkKernelArgument("-hwpfixlegacy")) requestMode = ModeLegacy;
    if (checkKernelArgument("-hwpfixautonomous")) requestMode = ModeAutonomous;
    auto result = lilu.onPatcherLoad(patchKernel);
    if (result != LiluAPI::Error::NoError) {
        setStatus("callback-registration-failed");
        SYSLOG("hwp", "patcher registration failed: %d", result);
    }
}
const char *disableArgs[] = {"-hwpfixoff"};
}

class EXPORT HWPRequestFix : public IOService {
    OSDeclareDefaultStructors(HWPRequestFix)
public:
    IOService *probe(IOService *provider, SInt32 *score) override {
        return ADDPR(startSuccess) ? IOService::probe(provider, score) : nullptr;
    }
    bool start(IOService *provider) override {
        if (!IOService::start(provider)) return false;
        service = this;
        serviceReady = true;
        activate();
        setProperty("Version", xStringify(MODULE_VERSION));
        setProperty("OldTopRequest", static_cast<uint64_t>(54), 32);
        setProperty("NewTopRequest", static_cast<uint64_t>(69), 32);
        registerService();
        return true;
    }
};
OSDefineMetaClassAndStructors(HWPRequestFix, IOService)

PluginConfiguration ADDPR(config) {
    "HWPRequestFix", parseModuleVersion(xStringify(MODULE_VERSION)), LiluAPI::AllowNormal,
    disableArgs, arrsize(disableArgs), nullptr, 0, nullptr, 0,
    static_cast<KernelVersion>(8), static_cast<KernelVersion>(255), pluginStart
};

extern "C" {
kern_return_t _start(kmod_info_t *, void *);
kern_return_t _stop(kmod_info_t *, void *);
kern_return_t HWPRequestFix_kern_start(kmod_info_t *, void *);
kern_return_t HWPRequestFix_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default")))
KMOD_EXPLICIT_DECL(local.y9000p.HWPRequestFix, xStringify(MODULE_VERSION), _start, _stop)
kmod_start_func_t *_realmain = HWPRequestFix_kern_start;
kmod_stop_func_t *_antimain = HWPRequestFix_kern_stop;
int _kext_apple_cc = __APPLE_CC__;
}

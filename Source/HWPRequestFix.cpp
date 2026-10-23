#include <Headers/kern_api.hpp>
#include <Headers/plugin_start.hpp>
#include <mach/mach_types.h>
#include "Payloads.hpp"
#include "KernelSignature.hpp"
#include "KernelGuards.hpp"
#include "CPUGuards.hpp"

namespace {
volatile uint8_t enabled = 0;
bool installed = false;
bool serviceReady = false;
const char *status = "not-installed";
IOService *service = nullptr;
char earlyBuild[32] {};
const char *signatureKind = "not-checked";
uint64_t symbolDistance = 0;
uint32_t signatureDifference = 0;
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
    service->setProperty("Active", enabled != 0);
    service->setProperty("HookStatus", status);
    service->setProperty("BuildAtPatchTime", earlyBuild);
    service->setProperty("SignatureKind", signatureKind);
    service->setProperty("SymbolDistance", symbolDistance, 64);
    service->setProperty("FirstSignatureDifference", static_cast<uint64_t>(signatureDifference), 32);
    service->setProperty("CPUProfileMatched", cpuProfileMatched);
    service->setProperty("CPUBrand", cpuIdentity.brand);
    service->setProperty("RequestMode", requestModeName());
}

void setStatus(const char *value) {
    status = value;
    SYSLOG("hwp", "status: %s", value);
    publish();
}

void activate() {
    if (installed && serviceReady) {
        __atomic_store_n(&enabled, requestMode, __ATOMIC_RELEASE);
        SYSLOG("hwp", "active: maximum 54 -> 69; desired mode %s; minimum/EPP unchanged",
               requestModeName());
    }
    publish();
}

void patchKernel(void *, KernelPatcher &patcher) {
    if (getKernelVersion() != static_cast<KernelVersion>(25) ||
        getKernelMinorVersion() != 6) {
        setStatus("unsupported-kernel");
        return;
    }
    cpuIdentity = readCPUIdentity();
    cpuProfileMatched = matchesCPUProfile(cpuIdentity);
    if (!cpuProfileMatched) {
        setStatus("unsupported-cpu-profile");
        return;
    }
    auto deadline = patcher.solveSymbol(KernelPatcher::KernelID, "_xcpm_get_deadline");
    auto init = patcher.solveSymbol(KernelPatcher::KernelID, "_xcpm_pkg_dvfs_init");
    auto osversion = reinterpret_cast<const char *>(
        patcher.solveSymbol(KernelPatcher::KernelID, "_osversion"));
    auto header = patcher.solveSymbol(KernelPatcher::KernelID, "__mh_execute_header");
    patcher.clearError();
    if (osversion) strlcpy(earlyBuild, osversion, sizeof(earlyBuild));
    if (!deadline) { setStatus("missing-xcpm-get-deadline"); return; }
    if (!init) { setStatus("missing-xcpm-pkg-dvfs-init"); return; }
    if (!header) { setStatus("missing-kernel-mach-header"); return; }
    symbolDistance = deadline - init;
    if (symbolDistance != 0x1d00) { setStatus("symbol-distance-mismatch"); return; }
    // kern.osversion is mutable and may be empty before userspace startup.
    if (!matchesKernelUUID(reinterpret_cast<const void *>(header), 8192, expectedKernelUUID)) {
        setStatus("kernel-uuid-mismatch"); return;
    }
    auto helper = deadline - 0x420;
    auto code = reinterpret_cast<const uint8_t *>(helper);
    if (memcmp(code, bootKCSignature, sizeof(bootKCSignature)) == 0) {
        signatureKind = "verified-bootkc";
    } else if (memcmp(code, kernelSignature, sizeof(kernelSignature)) == 0) {
        signatureKind = "verified-standalone";
    } else {
        while (signatureDifference < sizeof(bootKCSignature) &&
               code[signatureDifference] == bootKCSignature[signatureDifference]) ++signatureDifference;
        setStatus("signature-mismatch");
        return;
    }
    // Keep Lilu's trampoline inside rel32 reach, enabling atomic 8-byte entry writes.
    auto distance = static_cast<int64_t>(reinterpret_cast<uintptr_t>(&lilu) - helper);
    if (distance <= -0x70000000LL || distance >= 0x70000000LL) {
        setStatus("lilu-outside-near-jump-range");
        return;
    }

    // Both wrappers remain inert until both installations and IOKit startup succeed.
    uint8_t general[384], core[384];
    auto n1 = makePayload(general, hwpGeneralStart, hwpGeneralEnd, hwpGeneralGate, &enabled);
    auto n2 = makePayload(core, hwpCoreStart, hwpCoreEnd, hwpCoreGate, &enabled);
    if (!n1 || !n2) { setStatus("invalid-payload-layout-inert"); return; }
    patcher.clearError();
    auto t1 = patcher.routeBlock(helper + 0x80, general, n1, true);
    if (t1 == EINVAL || !t1 || patcher.getError() != KernelPatcher::Error::NoError) {
        patcher.clearError();
        setStatus("general-hook-failed-inert");
        return;
    }
    auto t2 = patcher.routeBlock(helper + 0xe8, core, n2, true);
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
        setProperty("VerifiedKernel", "25G241 / 25.6.0");
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
    static_cast<KernelVersion>(25), static_cast<KernelVersion>(25), pluginStart
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

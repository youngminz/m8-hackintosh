// AMDRadeonVADriver2 addresses the VP9 hooks need, per macOS build (unslid shared-cache addresses).
// slide = runtime AVD_CreateAVDAccelerator - avdCreate. The vtable slots are checked against these
// before anything is patched, and an unknown build turns VP9 off.
// Struct offsets (processor + 0x10, decoder + 0x58, ctx + 0xe0 / 0x98 / 0xd8 / 0x1f0 / 0x270 / 0x1d0 / 0x1c8,
// texture + 0x98) are the same in both builds (disassembly of the three methods diffed).
// New build: `DSC_DIR=<cache copy> python3 tools/re/dsc.py syms 'AMDRadeonVADriver2$' 'AVD_CreateAVDAccelerator$|ZTV.*AvdVcnAv1DecDataProcessor|AvdVcnAv1DecDataProcessor.*(ProcessCreateDecodeSession|initHWContext|SetDecodeMessageFields)'`
#pragma once
#include <stdint.h>
#include <string.h>
#include <sys/sysctl.h>

struct Vp9Build { const char *ver; uint64_t avdCreate, vtable, create, initHW, setMsg; };
static const struct Vp9Build kVp9Builds[] = {
    // build     AVD_CreateAVDAccelerator  vtable (+0x10 = slot 0)  ProcessCreateDecodeSession  initHWContext  SetDecodeMessageFields
    {"24H232", 0x7ffb0865fbb8ULL, 0x7ffb412b0a60ULL, 0x7ffb08645534ULL, 0x7ffb0864710cULL, 0x7ffb08645728ULL},  // macOS 15.8.2
    {"25G342", 0x7ffb10b009e0ULL, 0x7ffb41be7a88ULL, 0x7ffb10b42884ULL, 0x7ffb10b4428cULL, 0x7ffb10b42a80ULL},  // macOS 26.7.3
};

// The entry for the running macOS build, or NULL. ver receives the build string.
static inline const struct Vp9Build *vp9Build(char ver[32]) {
    size_t vl = 32;
    ver[0] = 0;
    sysctlbyname("kern.osversion", ver, &vl, NULL, 0);
    for (size_t i = 0; i < sizeof kVp9Builds / sizeof *kVp9Builds; i++)
        if (!strcmp(ver, kVp9Builds[i].ver)) return &kVp9Builds[i];
    return NULL;
}

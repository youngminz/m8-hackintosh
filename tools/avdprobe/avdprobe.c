// avdprobe: call the AMD VA driver (AMDRadeonVADriver2, "AVD" interface) directly and list the
// functions it advertises on this GPU. Decode functions are funcId 1; subId 8 = H.264, 0x400 and 0x800 = HEVC (AppleGVA used 0x800 for an 8 bit file),
// 0x1000000 = AV1 (AvdVAContext::GetVAFunctionType).
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef uint32_t (*CreateFn)(void **, void *, void *);

int main(int argc, char **argv) {
    void *h = dlopen("/System/Library/Extensions/AMDRadeonVADriver2.bundle/Contents/MacOS/AMDRadeonVADriver2", RTLD_NOW);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    CreateFn create = (CreateFn)dlsym(h, "AVD_CreateAVDAccelerator");
    io_iterator_t it;
    IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it);
    uint32_t services[32] = {0}; int n = 0;
    for (io_service_t s; (s = IOIteratorNext(it)) && n < 32;) {
        CFTypeRef c = IORegistryEntryCreateCFProperty(s, CFSTR("IOGVACodec"), NULL, 0);
        char name[128]; IORegistryEntryGetName(s, name);
        printf("accelerator %s: IOGVACodec %s\n", name, c ? "present" : "absent");
        if (c) services[n++] = s;
    }
    if (!n) { printf("no accelerator with IOGVACodec\n"); return 1; }
    struct { uint32_t index; uint32_t pad; uint32_t *services; uint32_t mask; uint32_t pad2; uint64_t x[16]; } ainfo = {0, 0, services, 1};
    uint64_t drv[64] = {0};
    void *accel = NULL;
    uint32_t r = create(&accel, &ainfo, drv);
    printf("AVD_CreateAVDAccelerator = %u, accel %p, version %x, name %.16s\n", r, accel, (uint32_t)drv[0], (char *)drv + 0x1c);
    if (r || !accel) return 1;
    struct Cap { uint32_t nEngines; uint32_t p0; uint8_t **engines; uint32_t nFuncs; uint32_t p1; uint8_t **funcs; } *cap = (struct Cap *)drv[1];
    printf("%u engines, %u functions\n", cap->nEngines, cap->nFuncs);
    for (uint32_t i = 0; i < cap->nEngines; i++)
        printf("  engine %u: id %u \"%s\"\n", i, *(uint32_t *)cap->engines[i], (char *)cap->engines[i] + 4);
    for (uint32_t i = 0; i < cap->nFuncs; i++) {
        uint8_t *f = cap->funcs[i];
        uint32_t id = *(uint32_t *)f, hw = *(uint32_t *)(f + 0x10);
        uint64_t sub = *(uint64_t *)(f + 8);
        const char *what = "";
        if (id == 1) what = sub == 8 ? "decode H.264" : sub == 0x400 ? "decode HEVC (0x400)" : sub == 0x800 ? "decode HEVC (0x800, the one AppleGVA uses)" : sub == 0x1000000 ? "decode AV1" : sub == 0x40 ? "decode (type 3)" : sub == 0x10 ? "decode (type 1)" : sub == 2 ? "decode MPEG-2" : "decode ?";
        printf("  func %2u: id %#x sub %#llx hw %u bandwidth %llu fmt %.4s %s\n", i, id, sub, hw, *(uint64_t *)(f + 0x18), (char *)f + 0x2c, what);
    }
    if (argc < 2 || strcmp(argv[1], "ctx")) return 0;
    // ctx [subId] [width] [height]: create a driver and one decode context for that function.
    uint64_t sub = argc > 2 ? strtoull(argv[2], NULL, 0) : 0x1000000;
    uint32_t w = argc > 3 ? atoi(argv[3]) : 1920, ht = argc > 4 ? atoi(argv[4]) : 1080;
    typedef uint32_t (*Fn2)(void *, void *);
    Fn2 *tbl = (Fn2 *)accel;  // [1] CreateDriver, [2] DeleteDriver, [3] CreateContexts, [4] DeleteContexts
    struct { uint32_t flags, pad; void *accel; uint64_t x[8]; } dc = {0, 0, accel};
    void *driver = NULL;
    r = tbl[1](&driver, &dc);
    printf("CreateDriver = %u, driver %p\n", r, driver);
    if (r || !driver) return 1;
    uint8_t *func = NULL;
    for (uint32_t i = 0; i < cap->nFuncs; i++)
        if (*(uint32_t *)cap->funcs[i] == 1 && *(uint64_t *)(cap->funcs[i] + 8) == sub) func = cap->funcs[i];
    if (!func) { printf("function sub %#llx not advertised\n", sub); return 1; }
    struct Func { uint32_t id, p0; uint64_t sub; uint32_t hw, p1; uint64_t bandwidth; uint32_t width, height; uint32_t inFmt, inCnt, outFmt, outCnt; struct Func *next; uint64_t x[8]; } fi = {0};
    fi.id = 1; fi.sub = sub; fi.hw = *(uint32_t *)(func + 0x10); fi.bandwidth = (uint64_t)w * ht * 60;
    fi.width = w; fi.height = ht; fi.inFmt = 'rawb'; fi.inCnt = 1; fi.outFmt = 'NV12'; fi.outCnt = 1;
    struct Func *funcs[1] = {&fi};
    void *ctxs[1] = {NULL};
    struct { uint32_t count, pad; struct Func **funcs; void **ctxs; uint64_t x[8]; } info = {1, 0, funcs, ctxs};
    struct { void *info; void *capOut; uint64_t x[8]; } ci = {&info, NULL};
    r = tbl[3](driver, &ci);
    printf("CreateContexts(sub %#llx, %ux%u) = %u, context %p\n", sub, w, ht, r, ctxs[0]);
    return r != 0;
}

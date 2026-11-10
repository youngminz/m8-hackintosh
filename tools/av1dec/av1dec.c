// av1dec: AV1 hardware decode on the M8 through the AMD VA driver (AMDRadeonVADriver2 "AVD"
// interface, AvdVcnAv1DecDataProcessor), which macOS ships but nothing calls.
//   av1dec FILE.obu N [out.nv12]   decode until N frames were shown; write the shown frames as NV12
// Struct layouts: side notes avd-decode-command-layout.md, avd-av1-picparam-layout.md.
#include "av1hdr.h"
#include <CoreVideo/CoreVideo.h>
#include <IOKit/IOKitLib.h>
#include <IOSurface/IOSurface.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef uint32_t (*Fn2)(void *, void *);
typedef uint32_t (*Fn3)(void *, void *, void *);
enum { F_CreateDriver = 1, F_DeleteDriver, F_CreateContexts, F_DeleteContexts, F_GetBsInfo, F_AllocBsPool, F_FreeBsPool,
       F_GetPixInfo, F_AllocPixPool, F_FreePixPool, F_Schedule, F_Query };

struct Func { uint32_t id, p0; uint64_t sub; uint32_t hw, p1; uint64_t bandwidth; uint32_t width, height, inFmt, inCnt, outFmt, outCnt; struct Func *next; uint64_t x[8]; };
struct Cmd { uint32_t funcId, pad; uint64_t subId; uint32_t engineId, pad2; void *context; void *param; void *dependsOn, *next; uint64_t x[4]; };
struct Top {                      // AvdAv1DecodeParam
    uint32_t *seq, *frame;        // 0x00, 0x08
    uint8_t curr_pic_idx, pad[7]; // 0x10
    IOSurfaceRef output;          // 0x18
    uint32_t ref_frame_map[8];    // 0x20
    uint32_t frame_refs[7];       // 0x40
    uint32_t pad5c;
    uint32_t *tiles;              // 0x60
    uint64_t x[8];
};

#define SLOTS 9
static void **tbl, *driver, *context;
static uint8_t *bsBuf;
static uint32_t bsSize = 0x800000;
static CVPixelBufferRef pix[SLOTS];
static int slotOfRef[8];
static int decoded, shown, wantShown, failed;
static FILE *outFile;
static double decodeMs, schedMs;

static double nowMs(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return mach_absolute_time() * (double)tb.numer / tb.denom / 1e6; }

static void writeNv12(int slot, int w, int h) {
    if (!outFile) return;
    CVPixelBufferLockBaseAddress(pix[slot], kCVPixelBufferLock_ReadOnly);
    for (int p = 0; p < 2; p++) {
        const uint8_t *base = CVPixelBufferGetBaseAddressOfPlane(pix[slot], p);
        size_t stride = CVPixelBufferGetBytesPerRowOfPlane(pix[slot], p);
        for (int y = 0; y < (p ? h / 2 : h); y++) fwrite(base + y * stride, 1, w, outFile);
    }
    CVPixelBufferUnlockBaseAddress(pix[slot], kCVPixelBufferLock_ReadOnly);
}

#include "avdfill.h"

static void onFrame(void *ctx, Av1Parser *p, const Av1Frame *f, const uint8_t *data) {
    if (failed || shown >= wantShown) return;
    const Av1Seq *s = &p->seq;
    if (f->show_existing_frame) {
        int slot = slotOfRef[f->frame_to_show_map_idx];
        if (slot >= 0) { writeNv12(slot, f->upscaled_width, f->frame_height); shown++; }
        if (f->frame_type == AV1_KEY) for (int i = 0; i < 8; i++) slotOfRef[i] = slot;
        av1_update_refs(p, f);
        return;
    }
    static uint32_t S[0x600 / 4], F[0xc00 / 4], *T;
    if (!T) T = calloc(1, 0x8014 + 16);
    memset(F, 0, sizeof F); memset(T, 0, 0x8014);
    fillSeq(S, s); fillFrame(F, s, f);
    // bitstream: the bytes from the first tile to the end of the last one
    uint32_t start = f->tile_offset[0], end = f->tile_offset[f->num_tiles_found - 1] + f->tile_size[f->num_tiles_found - 1];
    if (end - start > bsSize) { fprintf(stderr, "frame too big\n"); failed = 1; return; }
    memcpy(bsBuf, data + start, end - start);
    T[0] = 0; T[0x10 / 4] = end - start;
    for (int t = 0; t < f->num_tiles_found; t++) {
        int row = t / f->tile_cols, col = t % f->tile_cols;
        uint32_t *e = (uint32_t *)((uint8_t *)T + 0x14 + 0x200 * row + 8 * col);
        e[0] = f->tile_offset[t] - start; e[1] = f->tile_size[t];
    }
    int slot = -1;
    for (int c = 0; c < SLOTS && slot < 0; c++) { int used = 0; for (int i = 0; i < 8; i++) used |= slotOfRef[i] == c; if (!used) slot = c; }
    struct Top top = {0};
    top.seq = S; top.frame = F; top.curr_pic_idx = slot; top.output = CVPixelBufferGetIOSurface(pix[slot]); top.tiles = T;
    for (int i = 0; i < 8; i++) top.ref_frame_map[i] = slotOfRef[i] < 0 ? 0x7f : slotOfRef[i];
    for (int i = 0; i < 7; i++) top.frame_refs[i] = f->frame_is_intra ? 0x7f : top.ref_frame_map[f->ref_frame_idx[i]];
    struct Cmd cmd = {1, 0, 0x1000000, 4, 0, context, &top};
    struct Cmd *cmds[1] = {&cmd}; void *refcons[1] = {NULL};
    struct { uint32_t count, pad; void *arr; } cl = {1, 0, cmds}, rl = {1, 0, refcons};
    double t0 = nowMs();
    uint32_t r = ((Fn3)tbl[F_Schedule])(driver, &cl, &rl);
    schedMs += nowMs() - t0;
    struct { void *refCon; uint32_t engineId, wait, status, result; } qe = {refcons[0], 4, 1, 0, 0};
    struct { uint32_t count, pad; void *entries; } ql = {1, 0, &qe};
    uint32_t q = 0; int polls = 0;
    if (!r && getenv("POLL")) {   // poll instead of the blocking wait (which only wakes every 33 ms here)
        for (;;) {
            qe.wait = 0; qe.status = 0; qe.result = 0;
            q = ((Fn2)tbl[F_Query])(driver, &ql);
            if (q || qe.status != 2 || nowMs() - t0 > 2000) break;
            usleep(atoi(getenv("POLL"))); polls++;
        }
        if (decoded < 3) printf("  polled %d times, status %u result %u, %.2f ms\n", polls, qe.status, qe.result, nowMs() - t0);
        if (!q && qe.status == 1) { qe.wait = 1; q = ((Fn2)tbl[F_Query])(driver, &ql); }   // collect the result code
    } else if (!r) q = ((Fn2)tbl[F_Query])(driver, &ql);
    decodeMs += nowMs() - t0;
    if (decoded < 12 || r || q || qe.status != 1 || qe.result)
        printf("frame %d: type %d show %d %dx%d slot %d bytes %u -> schedule %u query %u status %u result %u\n", decoded, f->frame_type,
               f->show_frame, f->frame_width, f->frame_height, slot, end - start, r, q, qe.status, qe.result);
    if (r || q || qe.status != 1 || qe.result) { failed = 1; return; }
    decoded++;
    if (f->show_frame) { writeNv12(slot, f->upscaled_width, f->frame_height); shown++; }
    for (int i = 0; i < 8; i++) if ((f->refresh_frame_flags >> i) & 1) slotOfRef[i] = slot;
    av1_update_refs(p, f);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { fprintf(stderr, "usage: av1dec FILE.obu N [out.nv12]\n"); return 2; }
    FILE *fp = fopen(argv[1], "rb"); if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *d = malloc(sz); fread(d, 1, sz, fp); fclose(fp);
    wantShown = atoi(argv[2]);
    if (argc > 3) outFile = fopen(argv[3], "wb");
    // sequence header first (for the size)
    static Av1Parser probe, parser;
    wantShown = 0; av1_parse_temporal_unit(&probe, d, sz < 4096 ? sz : 4096, onFrame, NULL); wantShown = atoi(argv[2]);
    if (!probe.have_seq) { fprintf(stderr, "no sequence header\n"); return 1; }
    int w = probe.seq.max_frame_width, h = probe.seq.max_frame_height;
    printf("AV1 profile %d %dx%d %d bit\n", probe.seq.profile, w, h, probe.seq.bit_depth);
    if (probe.seq.bit_depth != 8) { fprintf(stderr, "only 8 bit here\n"); return 1; }

    void *lib = dlopen("/System/Library/Extensions/AMDRadeonVADriver2.bundle/Contents/MacOS/AMDRadeonVADriver2", RTLD_NOW);
    uint32_t (*create)(void **, void *, void *) = lib ? dlsym(lib, "AVD_CreateAVDAccelerator") : NULL;
    if (!create) { fprintf(stderr, "no AMD VA driver\n"); return 1; }
    io_iterator_t it; uint32_t services[8] = {0}; int n = 0;
    IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it);
    for (io_service_t sv; (sv = IOIteratorNext(it)) && n < 8;) {
        CFTypeRef c = IORegistryEntryCreateCFProperty(sv, CFSTR("IOGVACodec"), NULL, 0);
        if (c) services[n++] = sv;
    }
    struct { uint32_t index, pad; uint32_t *services; uint32_t mask, pad2; uint64_t x[16]; } ainfo = {0, 0, services, 1};
    uint64_t drv[64] = {0}; void *accel = NULL;
    uint32_t r = create(&accel, &ainfo, drv);
    if (r || !accel) { fprintf(stderr, "AVD_CreateAVDAccelerator = %u\n", r); return 1; }
    tbl = (void **)accel;
    struct { uint32_t flags, pad; void *accel; uint64_t x[8]; } dc = {0, 0, accel};
    r = ((Fn2)tbl[F_CreateDriver])(&driver, &dc);
    if (r || !driver) { fprintf(stderr, "CreateDriver = %u\n", r); return 1; }
    void *addrs[1] = {NULL};
    struct { uint32_t type, p0, size, fmt, one, count; void **addrs; uint64_t x[4]; } pool = {0, 0, bsSize, 'rawb', 1, 1, addrs};
    r = ((Fn2)tbl[F_AllocBsPool])(driver, &pool);
    bsBuf = addrs[0];
    printf("AllocateBitstreamBufferPool = %u, buffer %p\n", r, bsBuf);
    if (r || !bsBuf) return 1;
    struct Func fi = {0};
    fi.id = 1; fi.sub = 0x1000000; fi.hw = 4; fi.bandwidth = (uint64_t)w * h * 60; fi.width = w; fi.height = h;
    fi.inFmt = 'rawb'; fi.inCnt = 1; fi.outFmt = 'NV12'; fi.outCnt = 1;
    struct Func *funcs[1] = {&fi}; void *ctxs[1] = {NULL};
    struct { uint32_t count, pad; struct Func **funcs; void **ctxs; uint64_t x[8]; } info = {1, 0, funcs, ctxs};
    struct { void *info; void *capOut; uint64_t x[8]; } ci = {&info, NULL};
    r = ((Fn2)tbl[F_CreateContexts])(driver, &ci);
    context = ctxs[0];
    printf("CreateContexts(AV1) = %u, context %p\n", r, context);
    if (!r && context) {
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionaryRef empty = CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, empty);
        for (int i = 0; i < SLOTS; i++)
            if (CVPixelBufferCreate(NULL, w, h, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, attrs, &pix[i])) { fprintf(stderr, "pixel buffer\n"); return 1; }
        for (int i = 0; i < 8; i++) slotOfRef[i] = -1;
        int pr = av1_parse_temporal_unit(&parser, d, sz, onFrame, NULL);
        printf("parse result %d; decoded %d frames, shown %d, %.1f ms per decoded frame (schedule %.1f ms)%s\n", pr, decoded, shown, decoded ? decodeMs / decoded : 0, decoded ? schedMs / decoded : 0, failed ? " (FAILED)" : "");
    }
    if (outFile) fclose(outFile);
    ((Fn2)tbl[F_FreeBsPool])(driver, (void *)0);
    if (context) { struct { uint32_t count, pad; void **ctxs; } del = {1, 0, ctxs}; printf("DeleteContexts = %u\n", ((Fn2)tbl[F_DeleteContexts])(driver, &del)); }
    printf("DeleteDriver = %u\n", ((Fn2)tbl[F_DeleteDriver])(driver, NULL));
    return failed;
}

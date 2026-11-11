// vp9dec: VP9 hardware decode on the M8 (Radeon 660M, VCN 3.1.2) through the AMD VA driver.
//   vp9dec FILE.ivf N [out.nv12]
// The driver (AMDRadeonVADriver2) has no VP9 path, so this borrows its AV1 one: it asks the driver for
// an AV1 decoder (buffers, command submission, completion all stay the driver's) and replaces three
// virtual methods of AvdVcnAv1DecDataProcessor in this process:
//   ProcessCreateDecodeSession  -> the create message's stream type becomes VP9
//   initHWContext               -> the context buffer gets VP9's default probabilities
//   SetDecodeMessageFields      -> the AV1 message in the decode message becomes a VP9 one, and the
//                                  probability buffer gets VP9's table
// Addresses per macOS build are in vp9addr.h; every one is checked at run time before anything is patched.
#include "../av1dec/av1hdr.h"
#include "../av1dec/avdfill.h"
#include "vcn_vp9.h"
#include "vp9_probs.h"
#include "vp9hdr.h"
#include "vp9msg.h"
#include <CoreVideo/CoreVideo.h>
#include <IOKit/IOKitLib.h>
#include <IOSurface/IOSurface.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t (*Fn2)(void *, void *);
typedef uint32_t (*Fn3)(void *, void *, void *);
enum { F_CreateDriver = 1, F_DeleteDriver, F_CreateContexts, F_DeleteContexts, F_GetBsInfo, F_AllocBsPool, F_FreeBsPool,
       F_GetPixInfo, F_AllocPixPool, F_FreePixPool, F_Schedule, F_Query };
struct Func { uint32_t id, p0; uint64_t sub; uint32_t hw, p1; uint64_t bandwidth; uint32_t width, height, inFmt, inCnt, outFmt, outCnt; struct Func *next; uint64_t x[8]; };
struct Cmd { uint32_t funcId, pad; uint64_t subId; uint32_t engineId, pad2; void *context; void *param; void *dependsOn, *next; uint64_t x[4]; };
struct Top { uint32_t *seq, *frame; uint8_t curr_pic_idx, pad[7]; IOSurfaceRef output; uint32_t ref_frame_map[8]; uint32_t frame_refs[7]; uint32_t pad5c; uint32_t *tiles; uint64_t x[8]; };

#include "vp9addr.h"
// processor + 0x10 = VADecoder*, decoder + 0x58 = context struct, decoder + 0x110 = processor
// ctx + 0xe0 message CPU pointer (create), ctx + 0x98[ctx + 0xd8] message texture, ctx + 0x1f0[ctx + 0x270] seg/prob texture,
// ctx + 0x1d0 context-buffer texture, ctx + 0x1c8 its size, texture + 0x98 = CPU address

static uint64_t (*orgCreate)(void *, void *);
static uint64_t (*orgInitHW)(void *);
static uint64_t (*orgSetMsg)(void *, void *, void *, void *, uint64_t, uint64_t);
static int verbose = 1;
static int bps = 1;  // bytes per sample in the output (2 for 10 bit, P010)

// the frame being submitted (the hooks run inside ScheduleAVDCommands on this thread)
static struct { const Vp9Frame *h; uint32_t bsSize; uint8_t refMap[8]; uint8_t cur; int ok, calls; } g;
static Vp9MsgState gMsgState;

static uint8_t *P(void *base, size_t off) { return *(uint8_t **)((uint8_t *)base + off); }
static uint8_t *ctxOf(void *proc) { return P(P(proc, 0x10), 0x58); }

static uint64_t hookCreate(void *proc, void *info) {
    uint64_t r = orgCreate(proc, info);
    uint8_t *ctx = ctxOf(proc);
    uint32_t *msg = (uint32_t *)P(ctx, 0xe0);
    if (verbose) printf("  create session: header %x total %x type %u, stream_type %#x -> %#x, %ux%u\n", msg[0], msg[1], msg[3], msg[10], RDECODE_CODEC_VP9, msg[12], msg[13]);
    if (msg[0] == 0x28 && msg[3] == RDECODE_MSG_CREATE && msg[10] == RDECODE_CODEC_AV1) msg[10] = RDECODE_CODEC_VP9;
    else printf("  create session: unexpected message, left alone\n");
    return r;
}

static uint64_t hookInitHW(void *proc) {
    uint8_t *ctx = ctxOf(proc);
    uint8_t *buf = P(P(ctx, 0x1d0), 0x98);
    uint32_t size = *(uint32_t *)(ctx + 0x1c8);
    if (verbose) printf("  init hw context: buffer %p size %#x (VP9 needs %#x)\n", buf, size, VP9_HW_CONTEXT_SIZE(0));
    if (!buf || size < VP9_HW_CONTEXT_SIZE(1)) return orgInitHW(proc);
    memset(buf, 0, size);
    vp9_init_hw_context(buf);
    return 0;
}

static uint64_t hookSetMsg(void *proc, void *param, void *targetTex, void *info, uint64_t a, uint64_t b) {
    uint64_t r = orgSetMsg(proc, param, targetTex, info, a, b);
    g.calls++;
    if (r || !g.h) return r;
    uint8_t *ctx = ctxOf(proc);
    uint8_t *msgTex = P(ctx, 0x98 + 8 * (size_t)*(uint32_t *)(ctx + 0xd8));
    uint8_t *base = msgTex ? P(msgTex, 0x98) : NULL;
    uint8_t *probTex = P(ctx, 0x1f0 + 8 * (size_t)*(uint32_t *)(ctx + 0x270));
    uint8_t *prob = probTex ? P(probTex, 0x98) : NULL;
    rvcn_dec_message_header_t *hd = (rvcn_dec_message_header_t *)base;
    if (!base || !prob || hd->header_size != sizeof *hd || hd->msg_type != RDECODE_MSG_DECODE || hd->num_buffers > 8) {
        printf("  decode message not where expected (base %p prob %p)\n", base, prob);
        return r;
    }
    rvcn_dec_message_index_t *ix = hd->index;
    rvcn_dec_message_decode_t *dec = NULL; uint8_t *codec = NULL;
    for (unsigned i = 0; i < hd->num_buffers; i++) {
        if (ix[i].message_id == RDECODE_MESSAGE_DECODE) dec = (rvcn_dec_message_decode_t *)(base + ix[i].offset);
        if (ix[i].message_id == RDECODE_MESSAGE_AV1) {
            codec = base + ix[i].offset;
            if (verbose && g.calls <= 2) printf("  decode message: %u entries, total %#x; AV1 entry at %#x size %#x -> VP9 size %#zx\n", hd->num_buffers, hd->total_size, ix[i].offset, ix[i].size, sizeof(rvcn_dec_message_vp9_t));
            memset(codec, 0, ix[i].size);
            ix[i].message_id = RDECODE_MESSAGE_VP9; ix[i].size = sizeof(rvcn_dec_message_vp9_t);
        }
    }
    if (!dec || !codec || dec->stream_type != RDECODE_CODEC_AV1) { printf("  decode message entries missing\n"); return r; }
    if (verbose && g.calls <= 2)
        printf("  common: %ux%u bsd %#x dpb %#x dt %#x hwctx %#x swctx %#x db_pitch %u db_h %u flags %#x\n", dec->width_in_samples, dec->height_in_samples,
               dec->bsd_size, dec->dpb_size, dec->dt_size, dec->hw_ctxt_size, dec->sw_ctxt_size, dec->db_pitch, dec->db_aligned_height, dec->decode_flags);
    dec->stream_type = RDECODE_CODEC_VP9;
    dec->width_in_samples = g.h->frame_width; dec->height_in_samples = g.h->frame_height;
    dec->db_pitch = (g.h->frame_width + 63) & ~63u; dec->db_aligned_height = (g.h->frame_height + 63) & ~63u;
    dec->db_pitch_uv = 0;
    vp9_init_probs_buffer(prob);
    vp9_fill_msg(&gMsgState, g.h, g.bsSize, g.refMap, g.cur, bps == 2, (rvcn_dec_message_vp9_t *)codec, (rvcn_dec_vp9_probs_segment_t *)prob);
    g.ok = 1;
    return r;
}

static int patchVtable(void *lib) {
    uint64_t f = (uint64_t)dlsym(lib, "AVD_CreateAVDAccelerator");
    char ver[32];
    const struct Vp9Build *B = vp9Build(ver);
    if (!B) { fprintf(stderr, "no VP9 addresses for macOS build %s\n", ver); return 1; }
    int64_t slide = (int64_t)(f - B->avdCreate);
    uint64_t *vt = (uint64_t *)(B->vtable + slide + 0x10);
    if (vt[0x10 / 8] != B->create + slide || vt[0x60 / 8] != B->initHW + slide || vt[0x70 / 8] != B->setMsg + slide) {
        fprintf(stderr, "vtable does not match this build (%llx %llx %llx)\n", vt[2] - slide, vt[12] - slide, vt[14] - slide);
        return 1;
    }
    orgCreate = (void *)vt[0x10 / 8]; orgInitHW = (void *)vt[0x60 / 8]; orgSetMsg = (void *)vt[0x70 / 8];
    mach_vm_address_t page = (mach_vm_address_t)vt & ~0xfffULL;
    kern_return_t kr = mach_vm_protect(mach_task_self(), page, 0x2000, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr) { fprintf(stderr, "vm_protect: %d\n", kr); return 1; }
    vt[0x10 / 8] = (uint64_t)hookCreate; vt[0x60 / 8] = (uint64_t)hookInitHW; vt[0x70 / 8] = (uint64_t)hookSetMsg;
    mach_vm_protect(mach_task_self(), page, 0x2000, FALSE, VM_PROT_READ);
    printf("vtable patched (slide %#llx)\n", (long long)slide);
    return 0;
}

#define SLOTS 9
#define BS_SIZE 0x800000
static void **tbl, *driver, *context;
static uint8_t *bsBuf;
static CVPixelBufferRef pix[SLOTS];
static int slotOfRef[8], decoded, shown, failed;
static FILE *outFile;
static double decodeMs;
static double nowMs(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb); return mach_absolute_time() * (double)tb.numer / tb.denom / 1e6; }

static void writeNv12(int slot, int w, int h) {
    if (!outFile) return;
    CVPixelBufferLockBaseAddress(pix[slot], kCVPixelBufferLock_ReadOnly);
    for (int p = 0; p < 2; p++) {
        const uint8_t *base = CVPixelBufferGetBaseAddressOfPlane(pix[slot], p);
        size_t stride = CVPixelBufferGetBytesPerRowOfPlane(pix[slot], p);
        for (int y = 0; y < (p ? h / 2 : h); y++) fwrite(base + y * stride, 1, (size_t)w * bps, outFile);
    }
    CVPixelBufferUnlockBaseAddress(pix[slot], kCVPixelBufferLock_ReadOnly);
}

static void decodeFrame(Vp9Parser *p, const uint8_t *data, uint32_t size) {
    Vp9Frame h;
    int e = vp9_parse_frame_header(p, data, size, &h);
    if (e) { printf("parse error %d\n", e); failed = 1; return; }
    if (h.show_existing_frame) {
        int slot = slotOfRef[h.frame_to_show_map_idx];
        if (slot >= 0) { writeNv12(slot, p->ref[h.frame_to_show_map_idx].frame_width, p->ref[h.frame_to_show_map_idx].frame_height); shown++; }
        return;
    }
    if ((h.profile != 0 && h.profile != 2) || h.bit_depth > 10 || h.bit_depth != (bps == 2 ? 10 : 8)) { printf("only profile 0 (8 bit) and profile 2 (10 bit) here\n"); failed = 1; return; }
    // 32 tile columns (only possible at 8192 wide) decoded to a black frame with a success status; 16 work.
    if (h.tile_cols_log2 > 4 && !getenv("VP9_ANY_TILES")) { printf("more than 16 tile columns: not supported\n"); failed = 1; return; }
    uint32_t padded = (size + 127) & ~127u;
    if (padded > BS_SIZE) { failed = 1; return; }
    memcpy(bsBuf, data, size); memset(bsBuf + size, 0, padded - size);
    // a minimal AV1 parameter set so the driver's own code runs: one tile, this frame's size
    static uint32_t S[0x600 / 4], F[0xc00 / 4], *T;
    static Av1Seq seq; static Av1Frame fr;
    if (!T) T = calloc(1, 0x8014 + 16);
    memset(&seq, 0, sizeof seq); memset(&fr, 0, sizeof fr); memset(F, 0, sizeof F); memset(T, 0, 0x8014);
    seq.bit_depth = h.bit_depth; seq.max_frame_width = h.frame_width; seq.max_frame_height = h.frame_height; seq.num_planes = 3;
    fr.frame_width = fr.upscaled_width = h.frame_width; fr.frame_height = h.frame_height; fr.superres_denom = 8;
    fr.tile_cols = fr.tile_rows = 1; fr.mi_cols = 2 * ((h.frame_width + 7) >> 3); fr.mi_rows = 2 * ((h.frame_height + 7) >> 3);
    fr.mi_col_starts[1] = fr.mi_cols; fr.mi_row_starts[1] = fr.mi_rows; fr.cdef_damping = 3; fr.show_frame = 1; fr.refresh_frame_flags = 0xff;
    fillSeq(S, &seq); fillFrame(F, &seq, &fr);
    T[0x10 / 4] = padded; T[0x14 / 4] = 0; T[0x18 / 4] = padded;
    int slot = -1;
    for (int c = 0; c < SLOTS && slot < 0; c++) { int used = 0; for (int i = 0; i < 8; i++) used |= slotOfRef[i] == c; if (!used) slot = c; }
    struct Top top = {0};
    top.seq = S; top.frame = F; top.curr_pic_idx = slot; top.output = CVPixelBufferGetIOSurface(pix[slot]); top.tiles = T;
    for (int i = 0; i < 8; i++) top.ref_frame_map[i] = 0x7f;
    for (int i = 0; i < 7; i++) top.frame_refs[i] = 0x7f;
    g.h = &h; g.bsSize = size; g.cur = slot; g.ok = 0;
    for (int i = 0; i < 8; i++) g.refMap[i] = slotOfRef[i] < 0 ? VP9MSG_NO_REF : slotOfRef[i];
    struct Cmd cmd = {1, 0, 0x1000000, 4, 0, context, &top};
    struct Cmd *cmds[1] = {&cmd}; void *refcons[1] = {NULL};
    struct { uint32_t count, pad; void *arr; } cl = {1, 0, cmds}, rl = {1, 0, refcons};
    double t0 = nowMs();
    uint32_t r = ((Fn3)tbl[F_Schedule])(driver, &cl, &rl);
    struct { void *refCon; uint32_t engineId, wait, status, result; } qe = {refcons[0], 4, 1, 0, 0};
    struct { uint32_t count, pad; void *entries; } ql = {1, 0, &qe};
    uint32_t q = r ? 0 : ((Fn2)tbl[F_Query])(driver, &ql);
    decodeMs += nowMs() - t0;
    g.h = NULL;
    if (decoded < 8 || r || q || qe.status != 1 || qe.result || !g.ok)
        printf("frame %d: %s show %d %dx%d slot %d bytes %u refresh %02x -> schedule %u query %u status %u result %u%s\n", decoded, h.frame_type == VP9_KEY_FRAME ? "key" : h.intra_only ? "intra" : "inter",
               h.show_frame, h.frame_width, h.frame_height, slot, size, h.refresh_frame_flags, r, q, qe.status, qe.result, g.ok ? "" : " (message hook did not run)");
    if (r || q || qe.status != 1 || qe.result || !g.ok) { failed = 1; return; }
    decoded++;
    if (h.show_frame) { writeNv12(slot, h.frame_width, h.frame_height); shown++; }
    for (int i = 0; i < 8; i++) if ((h.refresh_frame_flags >> i) & 1) slotOfRef[i] = slot;
    vp9_update_refs(p, &h);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { fprintf(stderr, "usage: vp9dec FILE.ivf N [out.nv12]\n"); return 2; }
    FILE *fp = fopen(argv[1], "rb"); if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *d = malloc(sz); fread(d, 1, sz, fp); fclose(fp);
    if (sz < 32 || memcmp(d, "DKIF", 4) || memcmp(d + 8, "VP90", 4)) { fprintf(stderr, "not a VP9 ivf file\n"); return 1; }
    int w = d[12] | d[13] << 8, h = d[14] | d[15] << 8, want = atoi(argv[2]);
    if (argc > 3) outFile = fopen(argv[3], "wb");
    {   // bit depth from the first frame header
        Vp9Parser pp; Vp9Frame fh; vp9_parser_init(&pp);
        uint32_t fsz0 = d[32] | d[33] << 8 | d[34] << 16 | (uint32_t)d[35] << 24, off0[VP9_MAX_SUPERFRAME], fs0[VP9_MAX_SUPERFRAME];
        if (vp9_split_superframe(d + 44, fsz0, off0, fs0) && !vp9_parse_frame_header(&pp, d + 44 + off0[0], fs0[0], &fh)) bps = fh.bit_depth > 8 ? 2 : 1;
    }
    printf("VP9 %dx%d %d bit\n", w, h, bps == 2 ? 10 : 8);
    void *lib = dlopen("/System/Library/Extensions/AMDRadeonVADriver2.bundle/Contents/MacOS/AMDRadeonVADriver2", RTLD_NOW);
    uint32_t (*create)(void **, void *, void *) = lib ? dlsym(lib, "AVD_CreateAVDAccelerator") : NULL;
    if (!create) { fprintf(stderr, "no AMD VA driver\n"); return 1; }
    if (patchVtable(lib)) return 1;
    io_iterator_t it; uint32_t services[8] = {0}; int n = 0;
    IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it);
    for (io_service_t sv; (sv = IOIteratorNext(it)) && n < 8;) { CFTypeRef c = IORegistryEntryCreateCFProperty(sv, CFSTR("IOGVACodec"), NULL, 0); if (c) services[n++] = sv; }
    struct { uint32_t index, pad; uint32_t *services; uint32_t mask, pad2; uint64_t x[16]; } ainfo = {0, 0, services, 1};
    uint64_t drv[64] = {0}; void *accel = NULL;
    if (create(&accel, &ainfo, drv) || !accel) { fprintf(stderr, "AVD_CreateAVDAccelerator failed\n"); return 1; }
    tbl = (void **)accel;
    struct { uint32_t flags, pad; void *accel; uint64_t x[8]; } dc = {0, 0, accel};
    if (((Fn2)tbl[F_CreateDriver])(&driver, &dc) || !driver) { fprintf(stderr, "CreateDriver failed\n"); return 1; }
    void *addrs[1] = {NULL};
    struct { uint32_t type, p0, size, fmt, one, count; void **addrs; uint64_t x[4]; } pool = {0, 0, BS_SIZE, 'rawb', 1, 1, addrs};
    if (((Fn2)tbl[F_AllocBsPool])(driver, &pool) || !addrs[0]) { fprintf(stderr, "bitstream pool failed\n"); return 1; }
    bsBuf = addrs[0];
    struct Func fi = {0};
    fi.id = 1; fi.sub = 0x1000000; fi.hw = 4; fi.bandwidth = (uint64_t)w * h * 60; fi.width = w; fi.height = h;
    fi.inFmt = 'rawb'; fi.inCnt = 1; fi.outFmt = 'NV12'; fi.outCnt = 1;
    struct Func *funcs[1] = {&fi}; void *ctxs[1] = {NULL};
    struct { uint32_t count, pad; struct Func **funcs; void **ctxs; uint64_t x[8]; } info = {1, 0, funcs, ctxs};
    struct { void *info; void *capOut; uint64_t x[8]; } ci = {&info, NULL};
    uint32_t r = ((Fn2)tbl[F_CreateContexts])(driver, &ci);
    context = ctxs[0];
    printf("CreateContexts = %u, context %p\n", r, context);
    if (!r && context) {
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionaryRef empty = CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, empty);
        for (int i = 0; i < SLOTS; i++) if (CVPixelBufferCreate(NULL, w, h, bps == 2 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, attrs, &pix[i])) return 1;
        for (int i = 0; i < 8; i++) slotOfRef[i] = -1;
        static Vp9Parser parser; vp9_parser_init(&parser); vp9_msg_state_init(&gMsgState);
        for (long pos = 32; pos + 12 <= sz && shown < want && !failed;) {
            uint32_t fsz = d[pos] | d[pos + 1] << 8 | d[pos + 2] << 16 | (uint32_t)d[pos + 3] << 24;
            const uint8_t *sample = d + pos + 12; pos += 12 + fsz;
            if (pos > sz) break;
            uint32_t off[VP9_MAX_SUPERFRAME], fs[VP9_MAX_SUPERFRAME];
            int nf = vp9_split_superframe(sample, fsz, off, fs);
            for (int k = 0; k < nf && !failed; k++) decodeFrame(&parser, sample + off[k], fs[k]);
        }
        printf("decoded %d frames, shown %d, %.1f ms per decoded frame%s\n", decoded, shown, decoded ? decodeMs / decoded : 0, failed ? " (FAILED)" : "");
    }
    if (outFile) fclose(outFile);
    ((Fn2)tbl[F_FreeBsPool])(driver, (void *)0);
    if (context) { struct { uint32_t count, pad; void **ctxs; } del = {1, 0, ctxs}; printf("DeleteContexts = %u\n", ((Fn2)tbl[F_DeleteContexts])(driver, &del)); }
    printf("DeleteDriver = %u\n", ((Fn2)tbl[F_DeleteDriver])(driver, NULL));
    return failed;
}

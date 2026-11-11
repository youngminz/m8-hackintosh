// VideoToolbox video decoder for 'vp09' on the AMD VCN (Radeon 660M), built into AV1AMDDecoder.bundle
// next to the AV1 one (factory VP9AMD_CreateInstance). The AMD VA driver has no VP9 path: this asks it
// for an AV1 decoder and swaps three virtual methods of AvdVcnAv1DecDataProcessor (see vp9dec.c).
// The swap is in the class vtable, so it is process wide; the hooks only act on the thread that is
// inside one of this decoder's driver calls (tCur) and pass everything else (real AV1) through.
#include "../av1dec/av1hdr.h"
#include "../av1dec/avdfill.h"
#include "vcn_vp9.h"
#include "vp9_probs.h"
#include "vp9hdr.h"
#include "vp9msg.h"
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <IOKit/IOKitLib.h>
#include <IOSurface/IOSurface.h>
#include <VideoToolbox/VideoToolbox.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <os/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// ---- SPI
typedef struct OpaqueCMBaseObject *CMBaseObjectRef;
typedef struct OpaqueVTVideoDecoder *VTVideoDecoderRef;
typedef struct OpaqueVTVideoDecoderSession *VTVideoDecoderSession;
typedef struct OpaqueVTVideoDecoderFrame *VTVideoDecoderFrame;
#pragma pack(push, 4)
typedef struct {
    uint32_t version;
    size_t derivedStorageSize;
    Boolean (*equal)(CMBaseObjectRef, CMBaseObjectRef);
    OSStatus (*invalidate)(CMBaseObjectRef);
    void (*finalize)(CMBaseObjectRef);
    CFStringRef (*copyDebugDescription)(CMBaseObjectRef);
    OSStatus (*copyProperty)(CMBaseObjectRef, CFStringRef, CFAllocatorRef, void *);
    OSStatus (*setProperty)(CMBaseObjectRef, CFStringRef, CFTypeRef);
    OSStatus (*notificationBarrier)(CMBaseObjectRef);
    const void *protocolTable;
} CMBaseClass;
typedef struct { const void *reserved; const CMBaseClass *baseClass; } CMBaseVTable;
typedef struct {
    uint32_t version;
    OSStatus (*startSession)(VTVideoDecoderRef, VTVideoDecoderSession, CMVideoFormatDescriptionRef);
    OSStatus (*decodeFrame)(VTVideoDecoderRef, VTVideoDecoderFrame, CMSampleBufferRef, VTDecodeFrameFlags, VTDecodeInfoFlags *);
    OSStatus (*copySupportedPropertyDictionary)(VTVideoDecoderRef, CFDictionaryRef *);
    OSStatus (*setProperties)(VTVideoDecoderRef, CFDictionaryRef);
    OSStatus (*copySerializableProperties)(VTVideoDecoderRef, CFAllocatorRef, CFDictionaryRef *);
    Boolean (*canAcceptFormatDescription)(VTVideoDecoderRef, CMFormatDescriptionRef);
    OSStatus (*finishDelayedFrames)(VTVideoDecoderRef);
    void *reserved[12];
} VTVideoDecoderClass;
typedef struct { CMBaseVTable base; const VTVideoDecoderClass *videoDecoderClass; } VTVideoDecoderVTable;
#pragma pack(pop)
extern OSStatus CMDerivedObjectCreate(CFAllocatorRef, const CMBaseVTable *, uint64_t classID, CMBaseObjectRef *);
extern void *CMBaseObjectGetDerivedStorage(CMBaseObjectRef);
extern uint64_t VTVideoDecoderGetClassID(void);
extern CMBaseObjectRef VTVideoDecoderGetCMBaseObject(VTVideoDecoderRef);
extern CVPixelBufferPoolRef VTDecoderSessionGetPixelBufferPool(VTVideoDecoderSession);
extern OSStatus VTDecoderSessionSetPixelBufferAttributes(VTVideoDecoderSession, CFDictionaryRef);
extern OSStatus VTDecoderSessionEmitDecodedFrame(VTVideoDecoderSession, VTVideoDecoderFrame, OSStatus, VTDecodeInfoFlags, CVImageBufferRef);

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "VP9AMD: " fmt, ##__VA_ARGS__)

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


// ---- hooks
typedef struct { const Vp9Frame *h; uint32_t bsSize; uint8_t refMap[8]; uint8_t cur; int ok; Vp9MsgState *st; int p010; } Cur;
static __thread Cur *tCur;   // set while this thread is inside a driver call made by a VP9 session
static uint64_t (*orgCreate)(void *, void *);
static uint64_t (*orgInitHW)(void *);
static uint64_t (*orgSetMsg)(void *, void *, void *, void *, uint64_t, uint64_t);
static int gPatched;

static uint8_t *P(void *base, size_t off) { return *(uint8_t **)((uint8_t *)base + off); }
static uint8_t *ctxOf(void *proc) { return P(P(proc, 0x10), 0x58); }

static uint64_t hookCreate(void *proc, void *info) {
    uint64_t r = orgCreate(proc, info);
    if (!tCur) return r;
    uint32_t *msg = (uint32_t *)P(ctxOf(proc), 0xe0);
    if (msg && msg[0] == 0x28 && msg[3] == RDECODE_MSG_CREATE && msg[10] == RDECODE_CODEC_AV1) msg[10] = RDECODE_CODEC_VP9;
    else LOG("create session: unexpected message");
    return r;
}
static uint64_t hookInitHW(void *proc) {
    if (!tCur) return orgInitHW(proc);
    uint8_t *ctx = ctxOf(proc);
    uint8_t *buf = P(P(ctx, 0x1d0), 0x98);
    uint32_t size = *(uint32_t *)(ctx + 0x1c8);
    if (!buf || size < VP9_HW_CONTEXT_SIZE(1)) return orgInitHW(proc);
    memset(buf, 0, size);
    vp9_init_hw_context(buf);
    return 0;
}
static uint64_t hookSetMsg(void *proc, void *param, void *targetTex, void *info, uint64_t a, uint64_t b) {
    uint64_t r = orgSetMsg(proc, param, targetTex, info, a, b);
    Cur *c = tCur;
    if (r || !c || !c->h) return r;
    uint8_t *ctx = ctxOf(proc);
    uint8_t *msgTex = P(ctx, 0x98 + 8 * (size_t)*(uint32_t *)(ctx + 0xd8));
    uint8_t *base = msgTex ? P(msgTex, 0x98) : NULL;
    uint8_t *probTex = P(ctx, 0x1f0 + 8 * (size_t)*(uint32_t *)(ctx + 0x270));
    uint8_t *prob = probTex ? P(probTex, 0x98) : NULL;
    rvcn_dec_message_header_t *hd = (rvcn_dec_message_header_t *)base;
    if (!base || !prob || hd->header_size != sizeof *hd || hd->msg_type != RDECODE_MSG_DECODE || hd->num_buffers > 8) return r;
    rvcn_dec_message_index_t *ix = hd->index;
    rvcn_dec_message_decode_t *dec = NULL; uint8_t *codec = NULL;
    for (unsigned i = 0; i < hd->num_buffers; i++) {
        if (ix[i].message_id == RDECODE_MESSAGE_DECODE) dec = (rvcn_dec_message_decode_t *)(base + ix[i].offset);
        if (ix[i].message_id == RDECODE_MESSAGE_AV1) {
            codec = base + ix[i].offset;
            memset(codec, 0, ix[i].size);
            ix[i].message_id = RDECODE_MESSAGE_VP9; ix[i].size = sizeof(rvcn_dec_message_vp9_t);
        }
    }
    if (!dec || !codec || dec->stream_type != RDECODE_CODEC_AV1) return r;
    dec->stream_type = RDECODE_CODEC_VP9;
    dec->width_in_samples = c->h->frame_width; dec->height_in_samples = c->h->frame_height;
    dec->db_pitch = (c->h->frame_width + 63) & ~63u; dec->db_aligned_height = (c->h->frame_height + 63) & ~63u;
    dec->db_pitch_uv = 0;
    vp9_init_probs_buffer(prob);
    vp9_fill_msg(c->st, c->h, c->bsSize, c->refMap, c->cur, c->p010, (rvcn_dec_message_vp9_t *)codec, (rvcn_dec_vp9_probs_segment_t *)prob);
    c->ok = 1;
    return r;
}
static void patchOnce(void) {
    void *lib = dlopen("/System/Library/Extensions/AMDRadeonVADriver2.bundle/Contents/MacOS/AMDRadeonVADriver2", RTLD_NOW);
    uint64_t f = lib ? (uint64_t)dlsym(lib, "AVD_CreateAVDAccelerator") : 0;
    if (!f) return;
    char ver[32];
    const struct Vp9Build *B = vp9Build(ver);
    if (!B) { LOG("no VP9 addresses for macOS build %s", ver); return; }
    int64_t slide = (int64_t)(f - B->avdCreate);
    uint64_t *vt = (uint64_t *)(B->vtable + slide + 0x10);
    if (vt[0x10 / 8] != B->create + slide || vt[0x60 / 8] != B->initHW + slide || vt[0x70 / 8] != B->setMsg + slide) { LOG("driver vtable does not match this build"); return; }
    orgCreate = (void *)vt[0x10 / 8]; orgInitHW = (void *)vt[0x60 / 8]; orgSetMsg = (void *)vt[0x70 / 8];
    mach_vm_address_t page = (mach_vm_address_t)vt & ~0xfffULL;
    if (mach_vm_protect(mach_task_self(), page, 0x2000, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY)) { LOG("vm_protect failed"); return; }
    vt[0x10 / 8] = (uint64_t)hookCreate; vt[0x60 / 8] = (uint64_t)hookInitHW; vt[0x70 / 8] = (uint64_t)hookSetMsg;
    mach_vm_protect(mach_task_self(), page, 0x2000, FALSE, VM_PROT_READ);
    gPatched = 1;
}

// ---- session
#define SLOTS 9
#define BS_SIZE 0x1000000
typedef struct {
    VTVideoDecoderSession session;
    void **tbl, *driver, *context;
    uint8_t *bsBuf;
    int width, height, bitDepth;
    Vp9Parser parser; Vp9MsgState msgState;
    CVPixelBufferRef pix[SLOTS];
    int slotOfRef[8];
    uint32_t S[0x600 / 4], F[0xc00 / 4], *T;
    pthread_mutex_t lock;
} Dec;
typedef struct { Dec *d; } Storage;

static int avdOpen(Dec *d, int w, int h) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, patchOnce);
    if (!gPatched) return -1;
    void *lib = dlopen("/System/Library/Extensions/AMDRadeonVADriver2.bundle/Contents/MacOS/AMDRadeonVADriver2", RTLD_NOW);
    uint32_t (*create)(void **, void *, void *) = dlsym(lib, "AVD_CreateAVDAccelerator");
    io_iterator_t it; uint32_t services[8] = {0}; int n = 0;
    IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it);
    for (io_service_t sv; (sv = IOIteratorNext(it)) && n < 8;) { CFTypeRef c = IORegistryEntryCreateCFProperty(sv, CFSTR("IOGVACodec"), NULL, 0); if (c) { services[n++] = sv; CFRelease(c); } }
    IOObjectRelease(it);
    if (!n) return -2;
    struct { uint32_t index, pad; uint32_t *services; uint32_t mask, pad2; uint64_t x[16]; } ainfo = {0, 0, services, 1};
    uint64_t drv[64] = {0}; void *accel = NULL;
    if (create(&accel, &ainfo, drv) || !accel) return -3;
    d->tbl = (void **)accel;
    struct { uint32_t flags, pad; void *accel; uint64_t x[8]; } dc = {0, 0, accel};
    if (((Fn2)d->tbl[F_CreateDriver])(&d->driver, &dc) || !d->driver) return -4;
    void *addrs[1] = {NULL};
    struct { uint32_t type, p0, size, fmt, one, count; void **addrs; uint64_t x[4]; } pool = {0, 0, BS_SIZE, 'rawb', 1, 1, addrs};
    if (((Fn2)d->tbl[F_AllocBsPool])(d->driver, &pool) || !addrs[0]) return -5;
    d->bsBuf = addrs[0];
    struct Func fi = {0};
    fi.id = 1; fi.sub = 0x1000000; fi.hw = 4; fi.bandwidth = (uint64_t)w * h * 60; fi.width = w; fi.height = h;
    fi.inFmt = 'rawb'; fi.inCnt = 1; fi.outFmt = 'NV12'; fi.outCnt = 1;
    struct Func *funcs[1] = {&fi}; void *ctxs[1] = {NULL};
    struct { uint32_t count, pad; struct Func **funcs; void **ctxs; uint64_t x[8]; } info = {1, 0, funcs, ctxs};
    struct { void *info; void *capOut; uint64_t x[8]; } ci = {&info, NULL};
    Cur cur = {0}; tCur = &cur;
    uint32_t r = ((Fn2)d->tbl[F_CreateContexts])(d->driver, &ci);
    tCur = NULL;
    if (r || !ctxs[0]) return -6;
    d->context = ctxs[0];
    d->T = calloc(1, 0x8014 + 16);
    for (int i = 0; i < 8; i++) d->slotOfRef[i] = -1;
    vp9_parser_init(&d->parser); vp9_msg_state_init(&d->msgState);
    return 0;
}
static void avdClose(Dec *d) {
    if (d->driver) {
        Cur cur = {0}; tCur = &cur;
        if (d->bsBuf) ((Fn2)d->tbl[F_FreeBsPool])(d->driver, (void *)0);
        if (d->context) { void *ctxs[1] = {d->context}; struct { uint32_t count, pad; void **ctxs; } del = {1, 0, ctxs}; ((Fn2)d->tbl[F_DeleteContexts])(d->driver, &del); }
        ((Fn2)d->tbl[F_DeleteDriver])(d->driver, NULL);
        tCur = NULL;
    }
    for (int i = 0; i < SLOTS; i++) if (d->pix[i]) CVPixelBufferRelease(d->pix[i]);
    free(d->T);
}

// Decodes one frame; *shown gets the image to show (not retained), or stays as it was.
static OSStatus decodeOne(Dec *d, const uint8_t *data, uint32_t size, CVPixelBufferRef *shown) {
    Vp9Frame h;
    if (vp9_parse_frame_header(&d->parser, data, size, &h)) return kVTVideoDecoderBadDataErr;
    if (h.show_existing_frame) {
        int slot = d->slotOfRef[h.frame_to_show_map_idx];
        if (slot >= 0) *shown = d->pix[slot];
        return noErr;
    }
    if ((h.profile != 0 && h.profile != 2) || h.bit_depth != d->bitDepth) return kVTVideoDecoderUnsupportedDataFormatErr;
    // 32 tile columns (only possible at 8192 wide) decoded to a black frame with a success status; 16 work.
    if (h.tile_cols_log2 > 4) return kVTVideoDecoderUnsupportedDataFormatErr;
    if (h.frame_width > d->width || h.frame_height > d->height) return kVTVideoDecoderBadDataErr;
    uint32_t padded = (size + 127) & ~127u;
    if (padded > BS_SIZE) return kVTVideoDecoderBadDataErr;
    memcpy(d->bsBuf, data, size); memset(d->bsBuf + size, 0, padded - size);
    Av1Seq seq; Av1Frame fr;   // a minimal AV1 parameter set so the driver's own code runs
    memset(&seq, 0, sizeof seq); memset(&fr, 0, sizeof fr); memset(d->F, 0, sizeof d->F); memset(d->T, 0, 0x8014);
    seq.bit_depth = h.bit_depth; seq.max_frame_width = h.frame_width; seq.max_frame_height = h.frame_height; seq.num_planes = 3;
    fr.frame_width = fr.upscaled_width = h.frame_width; fr.frame_height = h.frame_height; fr.superres_denom = 8;
    fr.tile_cols = fr.tile_rows = 1; fr.mi_cols = 2 * ((h.frame_width + 7) >> 3); fr.mi_rows = 2 * ((h.frame_height + 7) >> 3);
    fr.mi_col_starts[1] = fr.mi_cols; fr.mi_row_starts[1] = fr.mi_rows; fr.cdef_damping = 3; fr.show_frame = 1; fr.refresh_frame_flags = 0xff;
    fillSeq(d->S, &seq); fillFrame(d->F, &seq, &fr);
    d->T[0x10 / 4] = padded; d->T[0x18 / 4] = padded;
    int slot = -1;
    for (int c = 0; c < SLOTS && slot < 0; c++) { int used = 0; for (int i = 0; i < 8; i++) used |= d->slotOfRef[i] == c; if (!used) slot = c; }
    CVPixelBufferRef img = NULL;
    CVPixelBufferPoolRef pool = VTDecoderSessionGetPixelBufferPool(d->session);
    if (!pool || CVPixelBufferPoolCreatePixelBuffer(NULL, pool, &img) || !img) return kVTAllocationFailedErr;
    if (d->pix[slot]) CVPixelBufferRelease(d->pix[slot]);
    d->pix[slot] = img;
    struct Top top = {0};
    top.seq = d->S; top.frame = d->F; top.curr_pic_idx = slot; top.output = CVPixelBufferGetIOSurface(img); top.tiles = d->T;
    for (int i = 0; i < 8; i++) top.ref_frame_map[i] = 0x7f;
    for (int i = 0; i < 7; i++) top.frame_refs[i] = 0x7f;
    if (!top.output) return kVTAllocationFailedErr;
    Cur cur = {&h, size, {0}, slot, 0, &d->msgState, d->bitDepth > 8};
    for (int i = 0; i < 8; i++) cur.refMap[i] = d->slotOfRef[i] < 0 ? VP9MSG_NO_REF : d->slotOfRef[i];
    struct Cmd cmd = {1, 0, 0x1000000, 4, 0, d->context, &top};
    struct Cmd *cmds[1] = {&cmd}; void *refcons[1] = {NULL};
    struct { uint32_t count, pad; void *arr; } cl = {1, 0, cmds}, rl = {1, 0, refcons};
    tCur = &cur;
    uint32_t r = ((Fn3)d->tbl[F_Schedule])(d->driver, &cl, &rl);
    struct { void *refCon; uint32_t engineId, wait, status, result; } qe = {refcons[0], 4, 1, 0, 0};
    struct { uint32_t count, pad; void *entries; } ql = {1, 0, &qe};
    uint32_t q = r ? 0 : ((Fn2)d->tbl[F_Query])(d->driver, &ql);
    tCur = NULL;
    if (r || q || qe.status != 1 || qe.result || !cur.ok) {
        LOG("decode failed: schedule %u query %u status %u result %u hook %d", r, q, qe.status, qe.result, cur.ok);
        return kVTVideoDecoderMalfunctionErr;
    }
    if (h.show_frame) *shown = img;
    for (int i = 0; i < 8; i++) if ((h.refresh_frame_flags >> i) & 1) d->slotOfRef[i] = slot;
    vp9_update_refs(&d->parser, &h);
    return noErr;
}

// ---- VTVideoDecoder
static Dec *decOf(VTVideoDecoderRef dec) { return ((Storage *)CMBaseObjectGetDerivedStorage(VTVideoDecoderGetCMBaseObject(dec)))->d; }

static OSStatus startSession(VTVideoDecoderRef dec, VTVideoDecoderSession session, CMVideoFormatDescriptionRef fmt) {
    Dec *d = decOf(dec);
    d->session = session;
    CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(fmt);
    d->width = dim.width; d->height = dim.height;
    // vpcC: version, flags[3], profile, level, bitDepth << 4 | chroma << 1 | range
    d->bitDepth = 8;
    CFDictionaryRef atoms = CMFormatDescriptionGetExtension(fmt, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
    CFDataRef vpcC = atoms ? CFDictionaryGetValue(atoms, CFSTR("vpcC")) : NULL;
    if (vpcC && CFGetTypeID(vpcC) == CFDataGetTypeID() && CFDataGetLength(vpcC) >= 7) d->bitDepth = CFDataGetBytePtr(vpcC)[6] >> 4;
    if (d->bitDepth != 8 && d->bitDepth != 10) { LOG("unsupported bit depth %d", d->bitDepth); return kVTVideoDecoderUnsupportedDataFormatErr; }
    int r = avdOpen(d, d->width, d->height);
    LOG("startSession %dx%d %d bit: AMD VA driver open = %d", d->width, d->height, d->bitDepth, r);
    if (r) return kVTVideoDecoderNotAvailableNowErr;
    int fourcc = d->bitDepth > 8 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    CFNumberRef nf = CFNumberCreate(NULL, kCFNumberIntType, &fourcc), nw = CFNumberCreate(NULL, kCFNumberIntType, &d->width), nh = CFNumberCreate(NULL, kCFNumberIntType, &d->height);
    CFDictionaryRef empty = CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void *keys[] = {kCVPixelBufferPixelFormatTypeKey, kCVPixelBufferWidthKey, kCVPixelBufferHeightKey, kCVPixelBufferIOSurfacePropertiesKey};
    const void *vals[] = {nf, nw, nh, empty};
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, keys, vals, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    OSStatus st = VTDecoderSessionSetPixelBufferAttributes(session, attrs);
    CFRelease(attrs); CFRelease(empty); CFRelease(nf); CFRelease(nw); CFRelease(nh);
    return st;
}

static OSStatus decodeFrame(VTVideoDecoderRef dec, VTVideoDecoderFrame frame, CMSampleBufferRef sb, VTDecodeFrameFlags flags, VTDecodeInfoFlags *infoOut) {
    Dec *d = decOf(dec);
    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
    size_t len = bb ? CMBlockBufferGetDataLength(bb) : 0;
    uint8_t *buf = len ? malloc(len) : NULL;
    OSStatus st = kVTVideoDecoderBadDataErr;
    CVPixelBufferRef img = NULL;
    pthread_mutex_lock(&d->lock);
    if (buf && CMBlockBufferCopyDataBytes(bb, 0, len, buf) == noErr) {
        uint32_t off[VP9_MAX_SUPERFRAME], fs[VP9_MAX_SUPERFRAME];
        int nf = vp9_split_superframe(buf, len, off, fs);
        CVPixelBufferRef shown = NULL;
        st = nf ? noErr : kVTVideoDecoderBadDataErr;
        for (int k = 0; k < nf && !st; k++) st = decodeOne(d, buf + off[k], fs[k], &shown);
        if (!st && shown) img = CVPixelBufferRetain(shown);
    }
    pthread_mutex_unlock(&d->lock);
    free(buf);
    VTDecodeInfoFlags info = (!st && !img) ? kVTDecodeInfo_FrameDropped : 0;
    if (infoOut) *infoOut = info;
    VTDecoderSessionEmitDecodedFrame(d->session, frame, st, info, img);
    if (img) CVPixelBufferRelease(img);
    return noErr;
}

static OSStatus copySupported(VTVideoDecoderRef dec, CFDictionaryRef *out) {
    *out = CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    return noErr;
}
static OSStatus setProperties(VTVideoDecoderRef dec, CFDictionaryRef props) { return noErr; }
static Boolean canAccept(VTVideoDecoderRef dec, CMFormatDescriptionRef fmt) {
    Dec *d = decOf(dec);
    CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(fmt);
    return CMFormatDescriptionGetMediaSubType(fmt) == 'vp09' && dim.width <= d->width && dim.height <= d->height;
}
static OSStatus finishDelayed(VTVideoDecoderRef dec) { return noErr; }
static OSStatus baseInvalidate(CMBaseObjectRef o) { return noErr; }
static void baseFinalize(CMBaseObjectRef o) {
    Storage *s = CMBaseObjectGetDerivedStorage(o);
    if (s->d) { avdClose(s->d); free(s->d); s->d = NULL; }
}
static CFStringRef baseDescription(CMBaseObjectRef o) { return CFSTR("<VP9AMD hardware decoder>"); }
static OSStatus baseCopyProperty(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef a, void *out) { return kVTPropertyNotSupportedErr; }
static OSStatus baseSetProperty(CMBaseObjectRef o, CFStringRef key, CFTypeRef v) { return kVTPropertyNotSupportedErr; }

static const CMBaseClass baseClass = {1, sizeof(Storage), NULL, baseInvalidate, baseFinalize, baseDescription, baseCopyProperty, baseSetProperty, NULL, NULL};
static const VTVideoDecoderClass decoderClass = {1, startSession, decodeFrame, copySupported, setProperties, NULL, canAccept, finishDelayed, {0}};
static const VTVideoDecoderVTable vtable = {{NULL, &baseClass}, &decoderClass};

__attribute__((visibility("default")))
OSStatus VP9AMD_CreateInstance(FourCharCode codecType, CFAllocatorRef allocator, VTVideoDecoderRef *out) {
    CMBaseObjectRef obj = NULL;
    OSStatus st = CMDerivedObjectCreate(allocator, &vtable.base, VTVideoDecoderGetClassID(), &obj);
    if (st) return st;
    Storage *s = CMBaseObjectGetDerivedStorage(obj);
    s->d = calloc(1, sizeof(Dec));
    pthread_mutex_init(&s->d->lock, NULL);
    *out = (VTVideoDecoderRef)obj;
    return noErr;
}

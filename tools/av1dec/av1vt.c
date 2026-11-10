// VideoToolbox video decoder for 'av01' that decodes on the GPU through the AMD VA driver's AV1 path
// (AMDRadeonVADriver2, AvdVcnAv1DecDataProcessor). Built as a bundle for /Library/Video/Plug-Ins
// (factory AV1AMD_CreateInstance) and linked into vtav1test, which registers it in process.
// The VTVideoDecoder / CMBaseObject SPI layouts (4-byte packed) were read from Apple's AV1DecoderSW.
#include "av1hdr.h"
#include "avdfill.h"
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <IOKit/IOKitLib.h>
#include <IOSurface/IOSurface.h>
#include <VideoToolbox/VideoToolbox.h>
#include <dlfcn.h>
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

#define LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "AV1AMD: " fmt, ##__VA_ARGS__)

// ---- AMD VA driver session
typedef uint32_t (*Fn2)(void *, void *);
typedef uint32_t (*Fn3)(void *, void *, void *);
enum { F_CreateDriver = 1, F_DeleteDriver, F_CreateContexts, F_DeleteContexts, F_GetBsInfo, F_AllocBsPool, F_FreeBsPool,
       F_GetPixInfo, F_AllocPixPool, F_FreePixPool, F_Schedule, F_Query };
struct Func { uint32_t id, p0; uint64_t sub; uint32_t hw, p1; uint64_t bandwidth; uint32_t width, height, inFmt, inCnt, outFmt, outCnt; struct Func *next; uint64_t x[8]; };
struct Cmd { uint32_t funcId, pad; uint64_t subId; uint32_t engineId, pad2; void *context; void *param; void *dependsOn, *next; uint64_t x[4]; };
struct Top { uint32_t *seq, *frame; uint8_t curr_pic_idx, pad[7]; IOSurfaceRef output; uint32_t ref_frame_map[8]; uint32_t frame_refs[7]; uint32_t pad5c; uint32_t *tiles; uint64_t x[8]; };

#define SLOTS 9
#define BS_SIZE 0x1000000
typedef struct {
    VTVideoDecoderSession session;
    void **tbl, *driver, *context;
    uint8_t *bsBuf;
    int width, height, bitDepth;
    Av1Parser parser;
    CVPixelBufferRef pix[SLOTS];
    int slotOfRef[8];
    uint32_t S[0x600 / 4], F[0xc00 / 4], *T;
    CVPixelBufferRef shownImage;  // image of the frame shown by the sample being decoded
    OSStatus error;
    pthread_mutex_t lock;
} Dec;
typedef struct { Dec *d; } Storage;

static int avdOpen(Dec *d, int w, int h) {
    void *lib = dlopen("/System/Library/Extensions/AMDRadeonVADriver2.bundle/Contents/MacOS/AMDRadeonVADriver2", RTLD_NOW);
    uint32_t (*create)(void **, void *, void *) = lib ? dlsym(lib, "AVD_CreateAVDAccelerator") : NULL;
    if (!create) return -1;
    io_iterator_t it; uint32_t services[8] = {0}; int n = 0;
    IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it);
    for (io_service_t sv; (sv = IOIteratorNext(it)) && n < 8;) {
        CFTypeRef c = IORegistryEntryCreateCFProperty(sv, CFSTR("IOGVACodec"), NULL, 0);
        if (c) { services[n++] = sv; CFRelease(c); }
    }
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
    if (((Fn2)d->tbl[F_CreateContexts])(d->driver, &ci) || !ctxs[0]) return -6;
    d->context = ctxs[0];
    d->T = calloc(1, 0x8014 + 16);
    for (int i = 0; i < 8; i++) d->slotOfRef[i] = -1;
    return 0;
}

static void avdClose(Dec *d) {
    if (d->driver) {
        if (d->bsBuf) ((Fn2)d->tbl[F_FreeBsPool])(d->driver, (void *)0);
        if (d->context) { void *ctxs[1] = {d->context}; struct { uint32_t count, pad; void **ctxs; } del = {1, 0, ctxs}; ((Fn2)d->tbl[F_DeleteContexts])(d->driver, &del); }
        ((Fn2)d->tbl[F_DeleteDriver])(d->driver, NULL);
    }
    for (int i = 0; i < SLOTS; i++) if (d->pix[i]) CVPixelBufferRelease(d->pix[i]);
    free(d->T);
}

static void setShown(Dec *d, CVPixelBufferRef img) {
    if (d->shownImage) CVPixelBufferRelease(d->shownImage);
    d->shownImage = img ? CVPixelBufferRetain(img) : NULL;
}

static void onFrame(void *ctx, Av1Parser *p, const Av1Frame *f, const uint8_t *data) {
    Dec *d = ctx;
    if (d->error) return;
    if (f->show_existing_frame) {
        int slot = d->slotOfRef[f->frame_to_show_map_idx];
        if (slot >= 0) setShown(d, d->pix[slot]);
        if (f->frame_type == AV1_KEY) for (int i = 0; i < 8; i++) d->slotOfRef[i] = slot;
        av1_update_refs(p, f);
        return;
    }
    if (!f->num_tiles_found || f->upscaled_width > d->width || f->frame_height > d->height) { d->error = kVTVideoDecoderBadDataErr; return; }
    if (p->seq.bit_depth != d->bitDepth || p->seq.mono_chrome || p->seq.profile != 0) { d->error = kVTVideoDecoderUnsupportedDataFormatErr; return; }
    memset(d->F, 0, sizeof d->F); memset(d->T, 0, 0x8014);
    fillSeq(d->S, &p->seq); fillFrame(d->F, &p->seq, f);
    uint32_t start = f->tile_offset[0], end = f->tile_offset[f->num_tiles_found - 1] + f->tile_size[f->num_tiles_found - 1];
    if (end - start > BS_SIZE) { d->error = kVTVideoDecoderBadDataErr; return; }
    memcpy(d->bsBuf, data + start, end - start);
    d->T[0x10 / 4] = end - start;
    for (int t = 0; t < f->num_tiles_found; t++) {
        uint32_t *e = (uint32_t *)((uint8_t *)d->T + 0x14 + 0x200 * (t / f->tile_cols) + 8 * (t % f->tile_cols));
        e[0] = f->tile_offset[t] - start; e[1] = f->tile_size[t];
    }
    int slot = -1;
    for (int c = 0; c < SLOTS && slot < 0; c++) { int used = 0; for (int i = 0; i < 8; i++) used |= d->slotOfRef[i] == c; if (!used) slot = c; }
    // a fresh buffer for every decoded frame: the client may still hold the one this slot showed before
    CVPixelBufferRef img = NULL;
    CVPixelBufferPoolRef pool = VTDecoderSessionGetPixelBufferPool(d->session);
    if (!pool || CVPixelBufferPoolCreatePixelBuffer(NULL, pool, &img) || !img) { d->error = kVTAllocationFailedErr; return; }
    if (d->pix[slot]) CVPixelBufferRelease(d->pix[slot]);
    d->pix[slot] = img;
    struct Top top = {0};
    top.seq = d->S; top.frame = d->F; top.curr_pic_idx = slot; top.output = CVPixelBufferGetIOSurface(img); top.tiles = d->T;
    for (int i = 0; i < 8; i++) top.ref_frame_map[i] = d->slotOfRef[i] < 0 ? 0x7f : d->slotOfRef[i];
    for (int i = 0; i < 7; i++) top.frame_refs[i] = f->frame_is_intra ? 0x7f : top.ref_frame_map[f->ref_frame_idx[i]];
    if (!top.output) { d->error = kVTAllocationFailedErr; return; }
    struct Cmd cmd = {1, 0, 0x1000000, 4, 0, d->context, &top};
    struct Cmd *cmds[1] = {&cmd}; void *refcons[1] = {NULL};
    struct { uint32_t count, pad; void *arr; } cl = {1, 0, cmds}, rl = {1, 0, refcons};
    uint32_t r = ((Fn3)d->tbl[F_Schedule])(d->driver, &cl, &rl);
    struct { void *refCon; uint32_t engineId, wait, status, result; } qe = {refcons[0], 4, 1, 0, 0};
    struct { uint32_t count, pad; void *entries; } ql = {1, 0, &qe};
    uint32_t q = r ? 0 : ((Fn2)d->tbl[F_Query])(d->driver, &ql);
    if (r || q || qe.status != 1 || qe.result) {
        LOG("decode failed: schedule %u query %u status %u result %u", r, q, qe.status, qe.result);
        d->error = kVTVideoDecoderMalfunctionErr; return;
    }
    if (f->show_frame) setShown(d, img);
    for (int i = 0; i < 8; i++) if ((f->refresh_frame_flags >> i) & 1) d->slotOfRef[i] = slot;
    av1_update_refs(p, f);
}

static void noFrame(void *ctx, Av1Parser *p, const Av1Frame *f, const uint8_t *data) {}

// ---- VTVideoDecoder
static Dec *decOf(VTVideoDecoderRef dec) { return ((Storage *)CMBaseObjectGetDerivedStorage(VTVideoDecoderGetCMBaseObject(dec)))->d; }

static OSStatus startSession(VTVideoDecoderRef dec, VTVideoDecoderSession session, CMVideoFormatDescriptionRef fmt) {
    Dec *d = decOf(dec);
    d->session = session;
    CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(fmt);
    // av1C: 4 bytes, then configOBUs (the sequence header)
    CFDictionaryRef atoms = CMFormatDescriptionGetExtension(fmt, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
    CFDataRef av1C = atoms ? CFDictionaryGetValue(atoms, CFSTR("av1C")) : NULL;
    if (av1C && CFGetTypeID(av1C) == CFDataGetTypeID() && CFDataGetLength(av1C) > 4)
        av1_parse_temporal_unit(&d->parser, CFDataGetBytePtr(av1C) + 4, CFDataGetLength(av1C) - 4, noFrame, d);
    if (d->parser.have_seq) {
        if (d->parser.seq.bit_depth > 10 || d->parser.seq.mono_chrome || d->parser.seq.profile != 0) { LOG("unsupported: profile %d depth %d", d->parser.seq.profile, d->parser.seq.bit_depth); return kVTVideoDecoderUnsupportedDataFormatErr; }
        if (d->parser.seq.max_frame_width > dim.width) dim.width = d->parser.seq.max_frame_width;
        if (d->parser.seq.max_frame_height > dim.height) dim.height = d->parser.seq.max_frame_height;
    }
    d->width = dim.width; d->height = dim.height;
    d->bitDepth = d->parser.have_seq ? d->parser.seq.bit_depth : 8;
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
        d->error = 0; setShown(d, NULL);
        int r = av1_parse_temporal_unit(&d->parser, buf, len, onFrame, d);
        st = d->error ? d->error : r ? kVTVideoDecoderBadDataErr : noErr;
        if (r) LOG("parse error %d", r);
        if (!st && d->shownImage) img = CVPixelBufferRetain(d->shownImage);
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
    return CMFormatDescriptionGetMediaSubType(fmt) == 'av01' && dim.width <= d->width && dim.height <= d->height;
}
static OSStatus finishDelayed(VTVideoDecoderRef dec) { return noErr; }

static OSStatus baseInvalidate(CMBaseObjectRef o) { return noErr; }
static void baseFinalize(CMBaseObjectRef o) {
    Storage *s = CMBaseObjectGetDerivedStorage(o);
    if (s->d) { setShown(s->d, NULL); avdClose(s->d); free(s->d); s->d = NULL; }
}
static CFStringRef baseDescription(CMBaseObjectRef o) { return CFSTR("<AV1AMD hardware decoder>"); }
static OSStatus baseCopyProperty(CMBaseObjectRef o, CFStringRef key, CFAllocatorRef a, void *out) { return kVTPropertyNotSupportedErr; }
static OSStatus baseSetProperty(CMBaseObjectRef o, CFStringRef key, CFTypeRef v) { return kVTPropertyNotSupportedErr; }

static const CMBaseClass baseClass = {1, sizeof(Storage), NULL, baseInvalidate, baseFinalize, baseDescription, baseCopyProperty, baseSetProperty, NULL, NULL};
static const VTVideoDecoderClass decoderClass = {1, startSession, decodeFrame, copySupported, setProperties, NULL, canAccept, finishDelayed, {0}};
static const VTVideoDecoderVTable vtable = {{NULL, &baseClass}, &decoderClass};

__attribute__((visibility("default")))
OSStatus AV1AMD_CreateInstance(FourCharCode codecType, CFAllocatorRef allocator, VTVideoDecoderRef *out) {
    CMBaseObjectRef obj = NULL;
    OSStatus st = CMDerivedObjectCreate(allocator, &vtable.base, VTVideoDecoderGetClassID(), &obj);
    if (st) return st;
    Storage *s = CMBaseObjectGetDerivedStorage(obj);
    s->d = calloc(1, sizeof(Dec));
    pthread_mutex_init(&s->d->lock, NULL);
    *out = (VTVideoDecoderRef)obj;
    return noErr;
}

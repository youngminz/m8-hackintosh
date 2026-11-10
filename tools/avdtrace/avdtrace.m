// avdtrace: log how AppleGVA drives the AMD VA driver (AMDRadeonVADriver2, "AVD" interface) while
// VideoToolbox hardware-decodes N frames of FILE. Run on the M8:
//   avdtrace FILE N [depth] > trace.txt
// Hooks AVD_CreateAVDAccelerator in this process (inline jump over its 14-byte push prologue) and
// replaces the 16 entry points of the function table it returns (accelerator + 8 .. + 0x80) with
// wrappers that hex-dump their arguments before and after the call, following pointers.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>

static FILE *out;
static int maxDepth = 3;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static int readable(uint64_t a, size_t n, uint8_t *buf) {
    if (a < 0x10000 || a >= 0x7ff800000000ULL) return 0;
    mach_vm_size_t got = 0;
    return mach_vm_read_overwrite(mach_task_self(), a, n, (mach_vm_address_t)buf, &got) == KERN_SUCCESS && got == n;
}

static uint64_t seen[4096]; static int nseen;
static void dump(const char *tag, uint64_t a, size_t n, int depth) {
    uint8_t buf[0x400];
    if (n > sizeof buf) n = sizeof buf;
    while (n >= 0x40 && !readable(a, n, buf)) n /= 2;
    if (!readable(a, n, buf)) { fprintf(out, "%*s%s %llx: unreadable\n", depth * 2, "", tag, a); return; }
    for (int i = 0; i < nseen; i++) if (seen[i] == a) { fprintf(out, "%*s%s %llx: (dumped above)\n", depth * 2, "", tag, a); return; }
    if (nseen < 4096) seen[nseen++] = a;
    // trim trailing zero rows
    size_t end = n;
    while (end > 0x20) { int z = 1; for (size_t i = end - 0x10; i < end; i++) if (buf[i]) z = 0; if (!z) break; end -= 0x10; }
    fprintf(out, "%*s%s %llx (%zx bytes%s):\n", depth * 2, "", tag, a, n, end < n ? ", zero tail cut" : "");
    for (size_t o = 0; o < end; o += 0x10) {
        fprintf(out, "%*s  +%03zx:", depth * 2, "", o);
        for (size_t i = 0; i < 0x10 && o + i < n; i += 4) fprintf(out, " %08x", *(uint32_t *)(buf + o + i));
        fprintf(out, "\n");
    }
    if (depth >= maxDepth) return;
    for (size_t o = 0; o + 8 <= end; o += 8) {
        uint64_t p = *(uint64_t *)(buf + o);
        uint8_t t[8];
        if (p >= 0x100000000ULL && p < 0x7ff800000000ULL && !(p & 3) && readable(p, 8, t)) {
            char s[32]; snprintf(s, sizeof s, "[+%zx]->", o);
            dump(s, p, 0x200, depth + 1);
        }
    }
}

static const char *names[16] = {"CreateDriver", "DeleteDriver", "CreateContexts", "DeleteContexts", "GetBitstreamBufferInfo",
    "AllocateBitstreamBufferPool", "FreeBitstreamBufferPool", "GetPixelBufferInfo", "AllocatePixelBufferPool", "FreePixelBufferPool",
    "ScheduleAVDCommands", "QueryAVDCommandStatus", "SetContextParameter", "GetContextParameter", "SetDriverParameter", "GetDriverParameter"};
typedef uint64_t (*Fn4)(uint64_t, uint64_t, uint64_t, uint64_t);
static Fn4 org[16];
static int calls[16];
static int callLimit = 12;  // full dumps for the first calls of each entry point

static uint64_t wrap(int i, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    pthread_mutex_lock(&lock);
    int n = calls[i]++;
    int full = n < callLimit;
    fprintf(out, "\n>>> %s #%d (%llx, %llx, %llx, %llx) thread %p\n", names[i], n, a, b, c, d, (void *)pthread_self());
    if (full) { nseen = 0; dump("arg1", a, 0x100, 0); dump("arg2", b, 0x200, 0); dump("arg3", c, 0x200, 0); }
    fflush(out);
    pthread_mutex_unlock(&lock);
    uint64_t r = org[i](a, b, c, d);
    pthread_mutex_lock(&lock);
    fprintf(out, "<<< %s #%d = %llx\n", names[i], n, r);
    if (full) { nseen = 0; dump("arg1'", a, 0x100, 0); dump("arg2'", b, 0x200, 0); dump("arg3'", c, 0x200, 0); }
    fflush(out);
    pthread_mutex_unlock(&lock);
    return r;
}
#define W(i) static uint64_t w##i(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { return wrap(i, a, b, c, d); }
W(0) W(1) W(2) W(3) W(4) W(5) W(6) W(7) W(8) W(9) W(10) W(11) W(12) W(13) W(14) W(15)
static Fn4 wrappers[16] = {w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15};

static uint8_t *tramp;
typedef uint32_t (*CreateFn)(uint64_t **, uint64_t, uint64_t);
static uint32_t hookCreate(uint64_t **accel, uint64_t info, uint64_t drvInfo) {
    fprintf(out, "\n>>> AVD_CreateAVDAccelerator(%p, %llx, %llx)\n", accel, info, drvInfo);
    nseen = 0; dump("info", info, 0x100, 0);
    uint32_t r = ((CreateFn)tramp)(accel, info, drvInfo);
    fprintf(out, "<<< AVD_CreateAVDAccelerator = %x, accel %p\n", r, accel ? *accel : NULL);
    nseen = 0; dump("drvInfo'", drvInfo, 0x100, 0);
    if (!r && accel && *accel) {
        uint64_t *t = *accel;
        nseen = 0; dump("accel", (uint64_t)t, 0x88, 0);
        for (int i = 0; i < 16; i++) { org[i] = (Fn4)t[1 + i]; t[1 + i] = (uint64_t)wrappers[i]; }
    }
    fflush(out);
    return r;
}

static int installHook(void) {
    void *h = dlopen("/System/Library/Extensions/AMDRadeonVADriver2.bundle/Contents/MacOS/AMDRadeonVADriver2", RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    uint8_t *f = dlsym(h, "AVD_CreateAVDAccelerator");
    if (!f) { fprintf(stderr, "no AVD_CreateAVDAccelerator\n"); return 1; }
    static const uint8_t prologue[14] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
    if (memcmp(f, prologue, 14)) { fprintf(stderr, "unexpected prologue\n"); return 1; }
    tramp = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    memcpy(tramp, prologue, 14);
    uint8_t jmp[14] = {0xff, 0x25, 0, 0, 0, 0};
    uint64_t back = (uint64_t)f + 14, to = (uint64_t)hookCreate;
    memcpy(jmp + 6, &back, 8); memcpy(tramp + 14, jmp, 14);
    memcpy(jmp + 6, &to, 8);
    mach_vm_address_t page = (mach_vm_address_t)f & ~0xfffULL;
    kern_return_t kr = mach_vm_protect(mach_task_self(), page, 0x2000, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr) { fprintf(stderr, "vm_protect rw: %d\n", kr); return 1; }
    memcpy(f, jmp, 14);
    kr = mach_vm_protect(mach_task_self(), page, 0x2000, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr) { fprintf(stderr, "vm_protect rx: %d\n", kr); return 1; }
    fprintf(out, "hook installed at %p\n", f);
    return 0;
}

static int frames;
static void cb(void *r, void *s, OSStatus st, VTDecodeInfoFlags f, CVImageBufferRef img, CMTime pts, CMTime d) {
    if (st == noErr && img) frames++;
    else fprintf(stderr, "decode callback status %d\n", (int)st);
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        out = stdout;
        if (argc < 3) { fprintf(stderr, "usage: avdtrace FILE N [depth] [calls]\n"); return 2; }
        if (argc > 3) maxDepth = atoi(argv[3]);
        if (argc > 4) callLimit = atoi(argv[4]);
        if (installHook()) return 1;
        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:@(argv[1])] options:nil];
        AVAssetTrack *track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!track) { fprintf(stderr, "no video track\n"); return 1; }
        CMVideoFormatDescriptionRef fmt = (__bridge CMVideoFormatDescriptionRef)track.formatDescriptions.firstObject;
        NSDictionary *spec = @{(id)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES, @"EnableSandboxedVideoDecoder": @NO};
        VTDecompressionOutputCallbackRecord rec = {cb, NULL};
        VTDecompressionSessionRef ses = NULL;
        OSStatus st = VTDecompressionSessionCreate(NULL, fmt, (__bridge CFDictionaryRef)spec, NULL, &rec, &ses);
        fprintf(out, "\n=== VTDecompressionSessionCreate = %d\n", (int)st);
        if (st) return 1;
        AVAssetReader *rd = [AVAssetReader assetReaderWithAsset:asset error:nil];
        AVAssetReaderTrackOutput *o = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:nil];
        [rd addOutput:o]; [rd startReading];
        int want = atoi(argv[2]), sent = 0;
        while (sent < want) {
            CMSampleBufferRef sb = [o copyNextSampleBuffer];
            if (!sb) break;
            if (CMSampleBufferGetNumSamples(sb)) {
                fprintf(out, "\n=== DecodeFrame %d (%zu bytes)\n", sent, CMSampleBufferGetTotalSampleSize(sb));
                VTDecompressionSessionDecodeFrame(ses, sb, 0, NULL, NULL);
                VTDecompressionSessionWaitForAsynchronousFrames(ses);
                sent++;
            }
            CFRelease(sb);
        }
        fprintf(out, "\n=== decoded %d/%d\n", frames, sent);
        VTDecompressionSessionInvalidate(ses);
        fprintf(out, "\n=== invalidated\n");
    }
    return 0;
}

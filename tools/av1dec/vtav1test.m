// vtav1test FILE.mp4 N [out.nv12] [system]: decode N frames of an AV1 file through VideoToolbox.
// Without "system" it registers the AMD AV1 decoder (av1vt.c) in this process first; with it, it
// uses whatever VideoToolbox finds (the installed bundle) and requires a hardware decoder.
#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>
extern OSStatus AV1AMD_CreateInstance(FourCharCode, CFAllocatorRef, void **);
extern void VTRegisterVideoDecoderWithInfo(CMVideoCodecType, CFDictionaryRef, OSStatus (*)(FourCharCode, CFAllocatorRef, void **));
extern OSStatus VTRegisterVideoDecoderBundleDirectory(CFURLRef);
extern OSStatus VTCopyVideoDecoderList(CFDictionaryRef, CFArrayRef *);
extern void VTRegisterVideoDecoder(CMVideoCodecType, OSStatus (*)(FourCharCode, CFAllocatorRef, void **));
static int frames; static FILE *outFile;
static void cb(void *r, void *s, OSStatus st, VTDecodeInfoFlags f, CVImageBufferRef img, CMTime pts, CMTime d) {
    if (st || !img) { fprintf(stderr, "callback status %d flags %x\n", (int)st, (unsigned)f); return; }
    frames++;
    if (!outFile) return;
    CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
    size_t w = CVPixelBufferGetWidth(img), h = CVPixelBufferGetHeight(img);
    for (int p = 0; p < 2; p++) {
        const uint8_t *b = CVPixelBufferGetBaseAddressOfPlane(img, p); size_t stride = CVPixelBufferGetBytesPerRowOfPlane(img, p);
        for (size_t y = 0; y < (p ? h / 2 : h); y++) fwrite(b + y * stride, 1, w, outFile);
    }
    CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
}
int main(int argc, const char **argv) {
    @autoreleasepool {
        setvbuf(stdout, NULL, _IONBF, 0);
        if (argc < 3) { fprintf(stderr, "usage: vtav1test FILE N [out.nv12] [system]\n"); return 2; }
        BOOL system = argc > 4 && !strcmp(argv[4], "system");
        if (getenv("BUNDLEDIR")) printf("VTRegisterVideoDecoderBundleDirectory = %d\n", (int)VTRegisterVideoDecoderBundleDirectory((__bridge CFURLRef)[NSURL fileURLWithPath:@(getenv("BUNDLEDIR")) isDirectory:YES]));
        if (!system && getenv("INFO")) VTRegisterVideoDecoderWithInfo('av01', (__bridge CFDictionaryRef)@{@"VTIsHardwareAccelerated": @YES}, AV1AMD_CreateInstance);
        else if (!system) VTRegisterVideoDecoder('av01', AV1AMD_CreateInstance);
        // CODEC=vp09 SUPP=1: VP9 decoders are "supplemental"; a client has to ask for them first (WebKit does).
        const char *cn = getenv("CODEC") ? getenv("CODEC") : "av01";
        CMVideoCodecType codec = (CMVideoCodecType)((uint32_t)cn[0] << 24 | (uint32_t)cn[1] << 16 | (uint32_t)cn[2] << 8 | (uint32_t)cn[3]);
        if (getenv("SUPP")) VTRegisterSupplementalVideoDecoderIfAvailable(codec);
        printf("VTIsHardwareDecodeSupported(%s) = %d\n", cn, VTIsHardwareDecodeSupported(codec));
        CFArrayRef list = NULL;
        VTCopyVideoDecoderList(NULL, &list);
        for (NSDictionary *e in (__bridge NSArray *)list)
            if ([[e description] containsString:@"AMD VCN"] || [[e description] containsString:@"1635135537"] || [[e description] containsString:@"1987063865"]) printf("decoder: %s\n", [[[e description] stringByReplacingOccurrencesOfString:@"\n" withString:@" "] UTF8String]);
        printf("%ld decoders listed\n", list ? CFArrayGetCount(list) : 0);
        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:@(argv[1])] options:nil];
        AVAssetTrack *track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!track) { fprintf(stderr, "no video track\n"); return 1; }
        CMVideoFormatDescriptionRef fmt = (__bridge CMVideoFormatDescriptionRef)track.formatDescriptions.firstObject;
        if (argc > 3 && strcmp(argv[3], "-")) outFile = fopen(argv[3], "wb");
        NSDictionary *spec = system ? @{(id)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES} : nil;
        if (getenv("SPEC_SW")) spec = @{@"EnableAppleOnlySWAV1": @YES};
        if (getenv("SPEC_NONE")) spec = nil;
        VTDecompressionOutputCallbackRecord rec = {cb, NULL};
        VTDecompressionSessionRef ses = NULL;
        OSStatus st = VTDecompressionSessionCreate(NULL, fmt, (__bridge CFDictionaryRef)spec, NULL, &rec, &ses);
        printf("VTDecompressionSessionCreate = %d\n", (int)st);
        if (st) return 1;
        CFBooleanRef hw = NULL;
        VTSessionCopyProperty(ses, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, NULL, &hw);
        printf("UsingHardwareAcceleratedVideoDecoder = %s\n", hw == kCFBooleanTrue ? "true" : hw ? "false" : "(none)");
        AVAssetReader *rd = [AVAssetReader assetReaderWithAsset:asset error:nil];
        AVAssetReaderTrackOutput *o = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:nil];
        [rd addOutput:o]; [rd startReading];
        int want = atoi(argv[2]), sent = 0;
        NSDate *t0 = [NSDate date];
        while (sent < want) {
            CMSampleBufferRef sb = [o copyNextSampleBuffer];
            if (!sb) break;
            if (CMSampleBufferGetNumSamples(sb)) { VTDecompressionSessionDecodeFrame(ses, sb, 0, NULL, NULL); sent++; }
            CFRelease(sb);
        }
        VTDecompressionSessionWaitForAsynchronousFrames(ses);
        double dt = -[t0 timeIntervalSinceNow];
        printf("decoded %d/%d frames in %.2f s (%.1f fps)\n", frames, sent, dt, frames / dt);
        VTDecompressionSessionInvalidate(ses); CFRelease(ses);
        if (outFile) fclose(outFile);
    }
    return 0;
}

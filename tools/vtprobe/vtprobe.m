// vtprobe: check VideoToolbox hardware video decode on the M8.
//   vtprobe query          -> VTIsHardwareDecodeSupported per codec (no engine use)
//   vtprobe session FILE   -> create a decompression session that REQUIRES hardware
//   vtprobe decode FILE N  -> decode N frames through that hardware session, report fps
//   vtprobe check FILE N   -> decode N frames in hardware and in software, compare frame N (luma),
//                             write hw.png / sw.png next to FILE
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>

static void query(void) {
    struct { const char *n; CMVideoCodecType t; } c[] = {
        {"h264", kCMVideoCodecType_H264}, {"hevc", kCMVideoCodecType_HEVC},
        {"vp9", 'vp09'}, {"av1", 'av01'}};
    for (int i = 0; i < 4; i++)
        printf("%-5s hardware decode: %s\n", c[i].n, VTIsHardwareDecodeSupported(c[i].t) ? "YES" : "no");
}

static int frames;
static void cb(void *r, void *s, OSStatus st, VTDecodeInfoFlags f, CVImageBufferRef img, CMTime pts, CMTime d) {
    if (st == noErr && img) frames++;
    else fprintf(stderr, "decode callback status %d\n", (int)st);
}

static CVPixelBufferRef lastImg;
static void keepCb(void *r, void *s, OSStatus st, VTDecodeInfoFlags f, CVImageBufferRef img, CMTime pts, CMTime d) {
    if (st == noErr && img) {
        frames++;
        if (lastImg) CVPixelBufferRelease(lastImg);
        lastImg = CVPixelBufferRetain(img);
    } else fprintf(stderr, "decode callback status %d\n", (int)st);
}

// Decodes the first n frames of track, hardware or software only; returns the last frame (retained).
static CVPixelBufferRef decodeFrames(AVAsset *asset, AVAssetTrack *track, CMVideoFormatDescriptionRef fmt, BOOL hw, int n, double *dt) {
    NSDictionary *spec = hw ? @{(id)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES}
                            : @{(id)kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder: @NO};
    NSDictionary *dst = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)};
    VTDecompressionOutputCallbackRecord rec = {keepCb, NULL};
    VTDecompressionSessionRef ses = NULL;
    frames = 0; lastImg = NULL;
    OSStatus st = VTDecompressionSessionCreate(NULL, fmt, (__bridge CFDictionaryRef)spec, (__bridge CFDictionaryRef)dst, &rec, &ses);
    if (st) { fprintf(stderr, "%s session: %d\n", hw ? "hw" : "sw", (int)st); return NULL; }
    NSError *err = nil;
    AVAssetReader *rd = [AVAssetReader assetReaderWithAsset:asset error:&err];
    AVAssetReaderTrackOutput *out = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:nil];
    [rd addOutput:out]; [rd startReading];
    NSDate *t0 = [NSDate date]; int sent = 0;
    while (sent < n) {
        CMSampleBufferRef sb = [out copyNextSampleBuffer];
        if (!sb) break;
        if (CMSampleBufferGetNumSamples(sb)) {
            VTDecompressionSessionDecodeFrame(ses, sb, 0, NULL, NULL);
            sent++;
            VTDecompressionSessionWaitForAsynchronousFrames(ses);
        }
        CFRelease(sb);
    }
    *dt = -[t0 timeIntervalSinceNow];
    VTDecompressionSessionInvalidate(ses);
    printf("%s: %d/%d frames, %.2f s\n", hw ? "hardware" : "software", frames, sent, *dt);
    return lastImg;
}

static void writePng(CVPixelBufferRef img, NSString *path) {
    CIImage *ci = [CIImage imageWithCVPixelBuffer:img];
    CIContext *ctx = [CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer: @YES}];
    CGImageRef cg = [ctx createCGImage:ci fromRect:ci.extent];
    CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path], CFSTR("public.png"), 1, NULL);
    CGImageDestinationAddImage(d, cg, NULL);
    CGImageDestinationFinalize(d);
    CFRelease(d); CGImageRelease(cg);
}

static int check(AVAsset *asset, AVAssetTrack *track, CMVideoFormatDescriptionRef fmt, int n, NSString *dir) {
    double dh, ds;
    CVPixelBufferRef h = decodeFrames(asset, track, fmt, YES, n, &dh);
    CVPixelBufferRef s = decodeFrames(asset, track, fmt, NO, n, &ds);
    if (!h || !s) return 1;
    CVPixelBufferLockBaseAddress(h, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(s, kCVPixelBufferLock_ReadOnly);
    size_t w = CVPixelBufferGetWidthOfPlane(h, 0), ht = CVPixelBufferGetHeightOfPlane(h, 0);
    const uint8_t *ph = CVPixelBufferGetBaseAddressOfPlane(h, 0), *ps = CVPixelBufferGetBaseAddressOfPlane(s, 0);
    size_t bh = CVPixelBufferGetBytesPerRowOfPlane(h, 0), bs = CVPixelBufferGetBytesPerRowOfPlane(s, 0);
    double sum = 0, mean = 0; size_t big = 0;
    for (size_t y = 0; y < ht; y++)
        for (size_t x = 0; x < w; x++) {
            int a = ph[y * bh + x], b = ps[y * bs + x];
            sum += abs(a - b); mean += a;
            if (abs(a - b) > 8) big++;
        }
    double px = (double)w * ht;
    printf("frame %d luma %zux%zu: mean |hw-sw| %.3f, pixels off by >8: %.3f%%, hw mean luma %.1f\n", n, w, ht, sum / px, 100.0 * big / px, mean / px);
    CVPixelBufferUnlockBaseAddress(h, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferUnlockBaseAddress(s, kCVPixelBufferLock_ReadOnly);
    writePng(h, [dir stringByAppendingPathComponent:@"hw.png"]);
    writePng(s, [dir stringByAppendingPathComponent:@"sw.png"]);
    printf("wrote hw.png, sw.png in %s\n", dir.UTF8String);
    return 0;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        setvbuf(stdout, NULL, _IONBF, 0);
        if (argc < 2 || !strcmp(argv[1], "query")) { query(); return 0; }
        if (argc < 3) { fprintf(stderr, "usage: vtprobe query|session FILE|decode FILE N\n"); return 2; }
        NSURL *url = [NSURL fileURLWithPath:@(argv[2])];
        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
        AVAssetTrack *track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!track) { fprintf(stderr, "no video track\n"); return 1; }
        CMVideoFormatDescriptionRef fmt = (__bridge CMVideoFormatDescriptionRef)track.formatDescriptions.firstObject;
        FourCharCode codec = CMFormatDescriptionGetMediaSubType(fmt);
        CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(fmt);
        printf("file: %.4s %dx%d\n", (char *)&(uint32_t){CFSwapInt32HostToBig(codec)}, dim.width, dim.height);
        if (!strcmp(argv[1], "check"))
            return check(asset, track, fmt, argc > 3 ? atoi(argv[3]) : 60, [@(argv[2]) stringByDeletingLastPathComponent]);
        NSDictionary *spec = @{(id)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES};
        VTDecompressionOutputCallbackRecord rec = {cb, NULL};
        VTDecompressionSessionRef ses = NULL;
        printf("creating hardware session...\n");
        OSStatus st = VTDecompressionSessionCreate(NULL, fmt, (__bridge CFDictionaryRef)spec, NULL, &rec, &ses);
        printf("VTDecompressionSessionCreate (require HW) = %d %s\n", (int)st, st ? "FAILED" : "ok");
        if (st || strcmp(argv[1], "decode")) return st ? 1 : 0;
        int want = argc > 3 ? atoi(argv[3]) : 60;
        NSError *err = nil;
        AVAssetReader *rd = [AVAssetReader assetReaderWithAsset:asset error:&err];
        AVAssetReaderTrackOutput *out = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:nil];
        [rd addOutput:out]; [rd startReading];
        NSDate *t0 = [NSDate date]; int sent = 0;
        while (sent < want) {
            CMSampleBufferRef sb = [out copyNextSampleBuffer];
            if (!sb) break;
            if (CMSampleBufferGetNumSamples(sb)) {
                st = VTDecompressionSessionDecodeFrame(ses, sb, 0, NULL, NULL);
                if (st) { fprintf(stderr, "DecodeFrame %d at frame %d\n", (int)st, sent); CFRelease(sb); break; }
                sent++;
                if (sent % 30 == 0) printf("sent %d, decoded %d\n", sent, frames);
            }
            CFRelease(sb);
        }
        VTDecompressionSessionWaitForAsynchronousFrames(ses);
        double dt = -[t0 timeIntervalSinceNow];
        printf("decoded %d/%d frames in %.2f s (%.1f fps)\n", frames, sent, dt, frames / dt);
        VTDecompressionSessionInvalidate(ses);
    }
    return 0;
}

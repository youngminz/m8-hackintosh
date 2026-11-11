// vtenc: check VideoToolbox hardware video encode on the M8.
//   vtenc query                   -> can a session that REQUIRES hardware be created, per codec
//   vtenc encode SRC CODEC N OUT  -> decode N frames of SRC (software, NV12), encode them with a
//                                    hardware-only session (CODEC = h264 | hevc | hevc10), write OUT (.mov)
// Measured 2026-11-11 (1080p, 8 Mbps): h264 202 fps, hevc 176 fps, PSNR vs source 44.9 / 46.2 dB (AMD gva encoder).
// hevc10: the encoder rejects Main10 (kVTPropertyNotSupportedErr, -12900) and writes 8-bit Main from P010 input.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>

static CMVideoCodecType codecOf(const char *s) {
    return !strcmp(s, "h264") ? kCMVideoCodecType_H264 : kCMVideoCodecType_HEVC;
}

static NSDictionary *hwSpec(void) {
    return @{(id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES};
}

static int query(void) {
    const char *names[] = {"h264", "hevc"};
    for (int i = 0; i < 2; i++) {
        VTCompressionSessionRef s = NULL;
        OSStatus st = VTCompressionSessionCreate(NULL, 1920, 1080, codecOf(names[i]), (__bridge CFDictionaryRef)hwSpec(),
                                                 NULL, NULL, NULL, NULL, &s);
        CFStringRef id = NULL;
        if (!st) VTSessionCopyProperty(s, kVTCompressionPropertyKey_EncoderID, NULL, &id);
        printf("%-5s hardware encode session: %s (%d) %s\n", names[i], st ? "no" : "YES", (int)st,
               id ? [(__bridge NSString *)id UTF8String] : "");
        if (id) CFRelease(id);
        if (s) { VTCompressionSessionInvalidate(s); CFRelease(s); }
    }
    return 0;
}

static AVAssetWriter *writer;
static AVAssetWriterInput *winput;
static int outFrames;
static size_t outBytes;

static void encCb(void *r, void *s, OSStatus st, VTEncodeInfoFlags f, CMSampleBufferRef sb) {
    if (st || !sb) { fprintf(stderr, "encode callback status %d\n", (int)st); return; }
    if (!writer) {
        CMFormatDescriptionRef fd = CMSampleBufferGetFormatDescription(sb);
        NSURL *u = (__bridge NSURL *)r;
        [[NSFileManager defaultManager] removeItemAtURL:u error:nil];
        writer = [AVAssetWriter assetWriterWithURL:u fileType:AVFileTypeQuickTimeMovie error:nil];
        winput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:nil sourceFormatHint:fd];
        [writer addInput:winput];
        [writer startWriting];
        [writer startSessionAtSourceTime:CMSampleBufferGetPresentationTimeStamp(sb)];
    }
    while (!winput.readyForMoreMediaData) usleep(1000);
    [winput appendSampleBuffer:sb];
    outFrames++;
    outBytes += CMSampleBufferGetTotalSampleSize(sb);
}

static int encode(const char *src, const char *codec, int n, const char *out) {
    BOOL ten = !strcmp(codec, "hevc10");
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:@(src)] options:nil];
    AVAssetTrack *track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
    if (!track) { fprintf(stderr, "no video track\n"); return 1; }
    CGSize sz = track.naturalSize;
    OSType pf = ten ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    AVAssetReader *rd = [AVAssetReader assetReaderWithAsset:asset error:nil];
    AVAssetReaderTrackOutput *ro = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track
        outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey: @(pf)}];
    [rd addOutput:ro]; [rd startReading];
    // Decode everything first so the timed part is the encode only.
    NSMutableArray *frames = [NSMutableArray array];
    NSMutableArray *ptss = [NSMutableArray array];
    while ((int)frames.count < n) {
        CMSampleBufferRef sb = [ro copyNextSampleBuffer];
        if (!sb) break;
        CVImageBufferRef img = CMSampleBufferGetImageBuffer(sb);
        if (img) {
            [frames addObject:(__bridge id)img];
            [ptss addObject:[NSValue valueWithCMTime:CMSampleBufferGetPresentationTimeStamp(sb)]];
        }
        CFRelease(sb);
    }
    printf("source: %dx%d, %lu frames ready (%s)\n", (int)sz.width, (int)sz.height, (unsigned long)frames.count, ten ? "P010" : "NV12");
    NSURL *ou = [NSURL fileURLWithPath:@(out)];
    VTCompressionSessionRef s = NULL;
    OSStatus st = VTCompressionSessionCreate(NULL, (int32_t)sz.width, (int32_t)sz.height, codecOf(codec),
                                             (__bridge CFDictionaryRef)hwSpec(), NULL, NULL, encCb, (__bridge void *)ou, &s);
    printf("VTCompressionSessionCreate (require HW) = %d %s\n", (int)st, st ? "FAILED" : "ok");
    if (st) return 1;
    CFStringRef id = NULL;
    VTSessionCopyProperty(s, kVTCompressionPropertyKey_EncoderID, NULL, &id);
    printf("encoder: %s\n", id ? [(__bridge NSString *)id UTF8String] : "?");
    if (ten) {
        st = VTSessionSetProperty(s, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_HEVC_Main10_AutoLevel);
        printf("set Main10 profile = %d\n", (int)st);
    }
    VTSessionSetProperty(s, kVTCompressionPropertyKey_AverageBitRate, (__bridge CFNumberRef)@(8000000));
    VTSessionSetProperty(s, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
    VTCompressionSessionPrepareToEncodeFrames(s);
    NSDate *t0 = [NSDate date];
    for (NSUInteger i = 0; i < frames.count; i++) {
        st = VTCompressionSessionEncodeFrame(s, (__bridge CVImageBufferRef)frames[i], [ptss[i] CMTimeValue],
                                             kCMTimeInvalid, NULL, NULL, NULL);
        if (st) { fprintf(stderr, "EncodeFrame %d at frame %lu\n", (int)st, (unsigned long)i); break; }
    }
    VTCompressionSessionCompleteFrames(s, kCMTimeInvalid);
    double dt = -[t0 timeIntervalSinceNow];
    printf("encoded %d frames in %.2f s (%.1f fps), %.2f MB\n", outFrames, dt, outFrames / dt, outBytes / 1e6);
    VTCompressionSessionInvalidate(s);
    if (writer) {
        [winput markAsFinished];
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        printf("wrote %s (status %ld)\n", out, (long)writer.status);
    }
    return outFrames ? 0 : 1;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        setvbuf(stdout, NULL, _IONBF, 0);
        if (argc < 2 || !strcmp(argv[1], "query")) return query();
        if (argc < 6 || strcmp(argv[1], "encode")) {
            fprintf(stderr, "usage: vtenc query | vtenc encode SRC h264|hevc|hevc10 N OUT.mov\n");
            return 2;
        }
        return encode(argv[2], argv[3], atoi(argv[4]), argv[5]);
    }
}

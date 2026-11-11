// mtlstress: Metal compute check for the M8 (runs on the target, shader compiled at runtime).
//   mtlstress SECONDS   -> integer hash kernel over 16M elements, checked against the CPU every pass,
//                          plus a float FMA kernel for throughput; prints passes, errors, GB/s, GFLOPS.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

static NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\n"
    "kernel void hashk(device uint *o [[buffer(0)]], constant uint &seed [[buffer(1)]], uint i [[thread_position_in_grid]]) {\n"
    "  uint x = i * 2654435761u + seed;\n"
    "  for (int k = 0; k < 16; k++) { x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16; }\n"
    "  o[i] = x;\n}\n"
    "kernel void fmak(device float4 *o [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
    "  float4 a = float4(i & 255) * 1e-3, b = float4(1.0001), c = float4(0.9999);\n"
    "  for (int k = 0; k < 1024; k++) { a = fma(a, b, c); a = fma(a, c, -b); }\n"
    "  o[i] = a;\n}\n";

static uint32_t hashc(uint32_t i, uint32_t seed) {
    uint32_t x = i * 2654435761u + seed;
    for (int k = 0; k < 16; k++) { x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16; }
    return x;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        setvbuf(stdout, NULL, _IONBF, 0);
        double secs = argc > 1 ? atof(argv[1]) : 30;
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) { printf("no Metal device\n"); return 1; }
        printf("device: %s\n", dev.name.UTF8String);
        NSError *err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
        if (!lib) { printf("compile failed: %s\n", err.description.UTF8String); return 1; }
        id<MTLComputePipelineState> ph = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"hashk"] error:&err];
        id<MTLComputePipelineState> pf = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"fmak"] error:&err];
        id<MTLCommandQueue> q = [dev newCommandQueue];
        const NSUInteger N = 16u << 20, NF = 1u << 20;
        id<MTLBuffer> out = [dev newBufferWithLength:N * 4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> fout = [dev newBufferWithLength:NF * 16 options:MTLResourceStorageModeShared];
        long passes = 0, bad = 0, gpuErr = 0; double hashT = 0, fmaT = 0;
        NSDate *t0 = [NSDate date];
        while (-[t0 timeIntervalSinceNow] < secs) {
            uint32_t seed = (uint32_t)arc4random();
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
            [e setComputePipelineState:ph]; [e setBuffer:out offset:0 atIndex:0]; [e setBytes:&seed length:4 atIndex:1];
            [e dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [e endEncoding];
            NSDate *s = [NSDate date]; [cb commit]; [cb waitUntilCompleted]; hashT += -[s timeIntervalSinceNow];
            if (cb.status != MTLCommandBufferStatusCompleted) { gpuErr++; printf("hash cb status %ld err %s\n", (long)cb.status, cb.error.description.UTF8String); continue; }
            const uint32_t *o = out.contents;
            for (NSUInteger i = 0; i < N; i += 997) if (o[i] != hashc((uint32_t)i, seed)) bad++;
            if (o[N - 1] != hashc((uint32_t)(N - 1), seed)) bad++;
            cb = [q commandBuffer]; e = [cb computeCommandEncoder];
            [e setComputePipelineState:pf]; [e setBuffer:fout offset:0 atIndex:0];
            [e dispatchThreads:MTLSizeMake(NF, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [e endEncoding];
            s = [NSDate date]; [cb commit]; [cb waitUntilCompleted]; fmaT += -[s timeIntervalSinceNow];
            if (cb.status != MTLCommandBufferStatusCompleted) { gpuErr++; printf("fma cb status %ld\n", (long)cb.status); }
            passes++;
        }
        double gflops = passes * (double)NF * 4 * 2 * 2048 / fmaT / 1e9;
        printf("passes %ld, hash mismatches %ld, gpu errors %ld, hash %.1f ms/pass, fma %.0f GFLOPS\n",
               passes, bad, gpuErr, hashT / passes * 1000, gflops);
        return bad || gpuErr;
    }
}

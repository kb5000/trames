// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurfaceRef.h>
#import <Metal/Metal.h>
#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

// Standalone feasibility probe; blocking waits below are test-only.
static void check(bool ok, const char* step, long status = 0) {
    if (!ok) { fprintf(stderr, "%s failed: %ld\n", step, status); exit(1); }
}
static void os(OSStatus status, const char* step) { check(status == noErr, step, status); }
static void vk(VkResult status, const char* step) { check(status == VK_SUCCESS, step, status); }
struct Encoded { CMSampleBufferRef sample = nullptr; OSStatus status = -1; };
struct Decoded { CVPixelBufferRef frame = nullptr; OSStatus status = -1; };
static void encoded(void* opaque, void*, OSStatus status, VTEncodeInfoFlags, CMSampleBufferRef sample) {
    auto* out = static_cast<Encoded*>(opaque);
    out->status = status;
    if (sample) out->sample = (CMSampleBufferRef)CFRetain(sample);
}
static void decoded(void* opaque, void*, OSStatus status, VTDecodeInfoFlags, CVImageBufferRef image, CMTime, CMTime) {
    auto* out = static_cast<Decoded*>(opaque);
    out->status = status;
    if (image) out->frame = CVPixelBufferRetain(image);
}

int main() { @autoreleasepool {
    NSMutableDictionary* result = [NSMutableDictionary dictionary];
    result[@"codec_hardware_query"] = @{
        @"h264": @(VTIsHardwareDecodeSupported(kCMVideoCodecType_H264)),
        @"hevc": @(VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)),
        @"vp9": @(VTIsHardwareDecodeSupported(kCMVideoCodecType_VP9)),
        @"av1": @(VTIsHardwareDecodeSupported(kCMVideoCodecType_AV1))
    };

    // Obtain exactly the Metal device used by MoltenVK.
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkExportMetalObjectCreateInfoEXT deviceExport = {VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
    deviceExport.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_DEVICE_BIT_EXT;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pNext = &deviceExport;
    ici.pApplicationInfo = &app;
    VkInstance instance;
    vk(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance");
    uint32_t count = 1;
    VkPhysicalDevice physical;
    vk(vkEnumeratePhysicalDevices(instance, &count, &physical), "vkEnumeratePhysicalDevices");
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    result[@"gpu"] = @(props.deviceName);
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    const char* deviceExts[] = {VK_EXT_METAL_OBJECTS_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExts;
    VkDevice device;
    vk(vkCreateDevice(physical, &dci, nullptr, &device), "vkCreateDevice");
    auto exportMetal = (PFN_vkExportMetalObjectsEXT)vkGetDeviceProcAddr(device, "vkExportMetalObjectsEXT");
    check(exportMetal != nullptr, "exportMetal entrypoint");
    VkExportMetalDeviceInfoEXT mdi = {VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT};
    VkExportMetalObjectsInfoEXT moi = {VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT};
    moi.pNext = &mdi;
    exportMetal(device, &moi);
    id<MTLDevice> metal = mdi.mtlDevice;
    check(metal != nil, "MoltenVK Metal device");

    const int width = 128, height = 96;
    NSDictionary* attributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (id)kCVPixelBufferMetalCompatibilityKey: @YES
    };
    CVPixelBufferRef input = nullptr;
    os(CVPixelBufferCreate(nullptr, width, height, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                           (CFDictionaryRef)attributes, &input), "create input");
    os(CVPixelBufferLockBaseAddress(input, 0), "lock input");
    for (int y = 0; y < height; y++)
        memset((char*)CVPixelBufferGetBaseAddressOfPlane(input, 0) + y * CVPixelBufferGetBytesPerRowOfPlane(input, 0), 96, width);
    for (int y = 0; y < height / 2; y++) {
        auto* row = (unsigned char*)CVPixelBufferGetBaseAddressOfPlane(input, 1) + y * CVPixelBufferGetBytesPerRowOfPlane(input, 1);
        for (int x = 0; x < width / 2; x++) { row[x * 2] = 64; row[x * 2 + 1] = 192; }
    }
    os(CVPixelBufferUnlockBaseAddress(input, 0), "unlock input");

    Encoded enc;
    VTCompressionSessionRef encoder = nullptr;
    NSDictionary* encSpec = @{(id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES};
    os(VTCompressionSessionCreate(nullptr, width, height, kCMVideoCodecType_H264,
        (CFDictionaryRef)encSpec, (CFDictionaryRef)attributes, nullptr, encoded, &enc, &encoder), "create encoder");
    os(VTSessionSetProperty(encoder, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue), "encoder realtime");
    os(VTSessionSetProperty(encoder, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse), "no reorder");
    os(VTCompressionSessionPrepareToEncodeFrames(encoder), "prepare encode");
    os(VTCompressionSessionEncodeFrame(encoder, input, CMTimeMake(0, 30), CMTimeMake(1, 30), nullptr, nullptr, nullptr), "encode");
    os(VTCompressionSessionCompleteFrames(encoder, kCMTimeInvalid), "finish encode");
    os(enc.status, "encode callback");
    check(enc.sample != nullptr, "compressed sample");
    result[@"compressed_bytes"] = @(CMSampleBufferGetTotalSampleSize(enc.sample));

    Decoded dec;
    VTDecompressionSessionRef decoder = nullptr;
    VTDecompressionOutputCallbackRecord cb = {decoded, &dec};
    NSDictionary* decSpec = @{(id)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES};
    os(VTDecompressionSessionCreate(nullptr, CMSampleBufferGetFormatDescription(enc.sample),
        (CFDictionaryRef)decSpec, (CFDictionaryRef)attributes, &cb, &decoder), "create decoder");
    CFTypeRef hardware = nullptr;
    os(VTSessionCopyProperty(decoder, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder,
        nullptr, &hardware), "hardware decode property");
    check(CFEqual(hardware, kCFBooleanTrue), "hardware decode required");
    result[@"actual_h264_hardware_decode"] = @YES;
    CFRelease(hardware);
    os(VTDecompressionSessionDecodeFrame(decoder, enc.sample, kVTDecodeFrame_EnableAsynchronousDecompression,
        nullptr, nullptr), "decode");
    os(VTDecompressionSessionWaitForAsynchronousFrames(decoder), "finish decode");
    os(dec.status, "decode callback");
    check(dec.frame != nullptr, "decoded frame");
    check(CVPixelBufferGetPixelFormatType(dec.frame) == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, "NV12 output");
    check(CVPixelBufferGetPlaneCount(dec.frame) == 2, "two planes");
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(dec.frame);
    check(surface != nullptr, "decoded IOSurface");
    result[@"iosurface"] = @YES;

    CVMetalTextureCacheRef cache = nullptr;
    os(CVMetalTextureCacheCreate(nullptr, nullptr, metal, nullptr, &cache), "texture cache");
    CVMetalTextureRef cvTextures[2] = {};
    id<MTLTexture> textures[2];
    VkImage images[2];
    NSMutableArray* planes = [NSMutableArray array];
    for (unsigned p = 0; p < 2; p++) {
        const size_t w = CVPixelBufferGetWidthOfPlane(dec.frame, p), h = CVPixelBufferGetHeightOfPlane(dec.frame, p);
        os(CVMetalTextureCacheCreateTextureFromImage(nullptr, cache, dec.frame, nullptr,
            p == 0 ? MTLPixelFormatR8Unorm : MTLPixelFormatRG8Unorm, w, h, p, &cvTextures[p]), "plane Metal texture");
        textures[p] = CVMetalTextureGetTexture(cvTextures[p]);
        check(textures[p] != nil && textures[p].iosurface == surface && textures[p].iosurfacePlane == p, "same IOSurface plane");
        VkImportMetalTextureInfoEXT imp = {VK_STRUCTURE_TYPE_IMPORT_METAL_TEXTURE_INFO_EXT};
        imp.plane = VK_IMAGE_ASPECT_COLOR_BIT;
        imp.mtlTexture = textures[p];
        VkExportMetalObjectCreateInfoEXT exp = {VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
        exp.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
        imp.pNext = &exp;
        VkImageCreateInfo image = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.pNext = &imp;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = p == 0 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8_UNORM;
        image.extent = {(uint32_t)w, (uint32_t)h, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vk(vkCreateImage(device, &image, nullptr, &images[p]), "import native Vulkan plane");
        VkExportMetalTextureInfoEXT out = {VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT};
        out.image = images[p];
        out.plane = VK_IMAGE_ASPECT_COLOR_BIT;
        moi.pNext = &out;
        exportMetal(device, &moi);
        check(out.mtlTexture == textures[p], "Vulkan retains exact Metal texture");
        [planes addObject:@{@"width": @(w), @"height": @(h),
            @"pitch": @(CVPixelBufferGetBytesPerRowOfPlane(dec.frame, p)),
            @"same_iosurface": @YES, @"same_vulkan_metal_texture": @YES}];
    }
    result[@"planes"] = planes;

    // GPU sampling oracle, with only the final three scalar results read by CPU.
    NSString* shader = @"#include <metal_stdlib>\nusing namespace metal;\nkernel void sample(texture2d<float,access::read> y [[texture(0)]], texture2d<float,access::read> uv [[texture(1)]], device float* out [[buffer(0)]]) { out[0]=y.read(uint2(32,32)).r; float4 c=uv.read(uint2(16,16)); out[1]=c.r; out[2]=c.g; }";
    NSError* error = nil;
    id<MTLLibrary> library = [metal newLibraryWithSource:shader options:nil error:&error];
    check(library != nil, error ? [[error description] UTF8String] : "Metal shader");
    id<MTLFunction> function = [library newFunctionWithName:@"sample"];
    id<MTLComputePipelineState> pipeline = [metal newComputePipelineStateWithFunction:function error:&error];
    check(pipeline != nil, "Metal compute pipeline");
    id<MTLBuffer> output = [metal newBufferWithLength:3 * sizeof(float) options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue = [metal newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> compute = [command computeCommandEncoder];
    [compute setComputePipelineState:pipeline];
    [compute setTexture:textures[0] atIndex:0];
    [compute setTexture:textures[1] atIndex:1];
    [compute setBuffer:output offset:0 atIndex:0];
    [compute dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
    [compute endEncoding];
    [command commit];
    [command waitUntilCompleted];
    check(command.status == MTLCommandBufferStatusCompleted, "GPU sampling completion");
    auto* values = (float*)output.contents;
    check(fabs(values[0] * 255 - 96) < 3 && fabs(values[1] * 255 - 64) < 3 && fabs(values[2] * 255 - 192) < 3, "decoded pixel oracle");
    result[@"gpu_sample_yuv"] = @[@(values[0] * 255), @(values[1] * 255), @(values[2] * 255)];
    result[@"decoded_frame_cpu_lock_or_copy"] = @NO;
    result[@"status"] = @"pass";
    NSData* json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:&error];
    fwrite(json.bytes, 1, json.length, stdout); putchar('\n');

    [output release]; [queue release]; [pipeline release];
    [function release]; [library release];
    for (unsigned p = 0; p < 2; p++) { vkDestroyImage(device, images[p], nullptr); CFRelease(cvTextures[p]); }
    CFRelease(cache);
    CVPixelBufferRelease(dec.frame);
    VTDecompressionSessionInvalidate(decoder); CFRelease(decoder);
    CFRelease(enc.sample);
    VTCompressionSessionInvalidate(encoder); CFRelease(encoder);
    CVPixelBufferRelease(input);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
} }

// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only oracle. Production presents the retained texture without readback.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int trames_zink_check_texture(void *address,uint32_t width,uint32_t height,uint32_t blue) {
    @autoreleasepool {
        // The embedding frame lease, not a guest pointer, authenticates and
        // retains this MTLTexture through producer completion and this read.
        id<MTLTexture> texture=(__bridge id<MTLTexture>)address;
        if(!texture || texture.width!=width || texture.height!=height ||
           texture.pixelFormat!=MTLPixelFormatBGRA8Unorm || !width || !height)return 1;
        id<MTLCommandQueue> queue=[texture.device newCommandQueue];
        NSUInteger stride=((NSUInteger)width*4+255)&~(NSUInteger)255;
        id<MTLBuffer> buffer=[texture.device newBufferWithLength:stride*height options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> command=[queue commandBuffer];
        id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
        if(!queue || !buffer || !command || !blit)return 2;
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                   sourceSize:MTLSizeMake(width,height,1) toBuffer:buffer destinationOffset:0
                   destinationBytesPerRow:stride destinationBytesPerImage:stride*height];
        [blit endEncoding];[command commit];[command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted){fprintf(stderr,"Metal oracle: %s\n",command.error.description.UTF8String);return 3;}
        const uint8_t *pixels=buffer.contents;
        for(uint32_t y=0;y<height;y++)for(uint32_t x=0;x<width;x++){
            const uint8_t *pixel=pixels+y*stride+x*4;
            // Wayland/Metal origin is top-left; GL's framebuffer origin is bottom-left.
            const uint8_t expected[4]={blue?255:0,y<height/2?255:0,x>=width/2?255:0,255};
            if(memcmp(pixel,expected,4)){
                fprintf(stderr,"Metal oracle %ux%u blue=%u at %u,%u: BGRA=%u,%u,%u,%u expected=%u,%u,%u,%u\n",width,height,blue,x,y,pixel[0],pixel[1],pixel[2],pixel[3],expected[0],expected[1],expected[2],expected[3]);return 4;
            }
        }
        printf("zink-metal-texture-ok: %ux%u blue=%u all exported GPU pixels correct\n",width,height,blue);fflush(stdout);
        return 0;
    }
}

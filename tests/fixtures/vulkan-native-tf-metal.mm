// SPDX-License-Identifier: GPL-3.0-or-later
// Direct Metal acceptance for the internal TF engine. This does not claim that
// the MoltenVK Vulkan extension/guest command adapter has been connected yet.
#import <Foundation/Foundation.h>
#include "metal.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace trames::tf;
static id<MTLDevice> device;
static id<MTLCommandQueue> queue;
static uint64_t scalars_checked = 0, pixels_checked = 0, maps_checked = 0;
static void check(bool value, const char* what) { if (!value) throw std::runtime_error(what); }
static Buffer buffer(NSUInteger size, uint8_t initial = 0xa5) {
    auto object = [device newBufferWithLength:std::max<NSUInteger>(size, 4) options:MTLResourceStorageModeShared];
    check(object != nil, "buffer allocation");std::memset(object.contents, initial, object.length);
    return {object, 0, object.length};
}
static uint8_t* bytes(Buffer b) { return static_cast<uint8_t*>(b.object.contents)+b.offset; }
static uint32_t word(Buffer b, NSUInteger at) { uint32_t n;std::memcpy(&n, bytes(b)+at, 4);return n; }
static void commit(id<MTLCommandBuffer> command) {
    [command commit];[command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted)
        throw std::runtime_error(command.error.description.UTF8String ?: "Metal submission failed");
}
static NSString* contents(NSString* path) {
    NSError* error = nil;auto text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&error];
    if (!text) throw std::runtime_error(error.description.UTF8String ?: "missing shader");return text;
}
static id<MTLLibrary> library(NSString* source) {
    NSError* error = nil;auto options = [MTLCompileOptions new];
    auto result = [device newLibraryWithSource:source options:options error:&error];
    if (!result) throw std::runtime_error(error.description.UTF8String ?: "Metal shader compilation failed");return result;
}
static id<MTLComputePipelineState> capture_pipeline(id<MTLLibrary> lib, NSString* entry, unsigned index_type) {
    auto desc = [MTLComputePipelineDescriptor new];desc.computeFunction = [lib newFunctionWithName:entry];
    desc.stageInputDescriptor = [MTLStageInputOutputDescriptor stageInputOutputDescriptor];
    auto input = desc.stageInputDescriptor;
    input.attributes[0].format = MTLAttributeFormatFloat4;input.attributes[0].bufferIndex = 16;
    input.layouts[16].stride = 16;input.layouts[16].stepRate = 1;
    input.layouts[16].stepFunction = index_type ? MTLStepFunctionThreadPositionInGridXIndexed : MTLStepFunctionThreadPositionInGridX;
    input.indexBufferIndex = 21;input.indexType = index_type == 16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
    NSError* error = nil;
    auto pipe = [device newComputePipelineStateWithDescriptor:desc options:MTLPipelineOptionNone reflection:nil error:&error];
    if (!pipe) throw std::runtime_error(error.description.UTF8String ?: "capture pipeline failed");return pipe;
}
static void capture(id<MTLCommandBuffer> command, id<MTLComputePipelineState> pipeline, const Parameters& p,
                    Buffer rows, Buffer stats, Buffer positions, Buffer indices = {}, int base = 0, unsigned first_instance = 0) {
    if (!p.vertices || !p.instances) return;
    auto encoder = [command computeCommandEncoder];[encoder setComputePipelineState:pipeline];
    [encoder setBuffer:stats.object offset:stats.offset atIndex:0];
    [encoder setBuffer:positions.object offset:positions.offset atIndex:16];
    if (indices.object) [encoder setBuffer:indices.object offset:indices.offset+p.first_index*(p.index_type/8) atIndex:21];
    [encoder setBuffer:rows.object offset:rows.offset atIndex:28];
    [encoder setStageInRegion:MTLRegionMake2D(base, first_instance, p.vertices, p.instances)];
    [encoder dispatchThreads:MTLSizeMake(p.vertices, p.instances, 1)
        threadsPerThreadgroup:MTLSizeMake(std::min<NSUInteger>(p.vertices, pipeline.threadExecutionWidth), 1, 1)];
    [encoder endEncoding];
}
static void check_record(Buffer first, Buffer second, unsigned record, unsigned vertex, unsigned instance) {
    float data[] = {1, 0.25f, 0.5f, float(vertex), float(vertex)+10, float(vertex)+20,
                    float(vertex)+30, float(vertex)+1000, float(vertex)+2000, float(vertex)+4000};
    unsigned offsets[] = {4,8,12,24,28,32,36,40,44,48};
    for (unsigned i = 0; i < 10; ++i) {
        check(!std::memcmp(bytes(first)+record*64+offsets[i], data+i, 4), "GPU TF float/matrix/array value");++scalars_checked;
    }
    check(word(first, record*64+16) == vertex && word(first, record*64+20) == instance, "GPU TF integer IDs");
    check(word(second, record*16+4) == 0x7fc12345u+vertex, "GPU TF integer bit pattern");scalars_checked += 3;
}
static void check_holes(Buffer first, Buffer second, unsigned written) {
    for (NSUInteger i = 0; i < first.size; ++i) {
        unsigned slot = unsigned(i/64), at = unsigned(i%64);
        bool used = slot < written && at >= 4 && at < 52;
        if (!used) check(bytes(first)[i] == 0xa5, "first TF range/padding sentinel");
    }
    for (NSUInteger i = 0; i < second.size; ++i) {
        unsigned slot = unsigned(i/16), at = unsigned(i%16);
        bool used = slot < written && at >= 4 && at < 8;
        if (!used) check(bytes(second)[i] == 0xa5, "second TF range/padding sentinel");
    }
}
static void check_draw(Buffer draw, Buffer map, Buffer state, Buffer indirect,
                        const std::vector<unsigned>& expected, unsigned written, unsigned primitive_size) {
    check(word(draw, 0) == written && word(draw, 4) == expected.size() && !word(draw, 8), "GPU assembly counts");
    for (unsigned i = 0; i < expected.size(); ++i) {
        check(word(map, i*4) == expected[i], "GPU primitive order/restart/instance map");++maps_checked;
    }
    State result;std::memcpy(&result, bytes(state), sizeof(result));
    check(result.written == written/primitive_size && result.needed == expected.size()/primitive_size, "64-bit GPU TF statistics");
    check(word(indirect, 0) == expected.size() && word(indirect, 4) == 1 && !word(indirect, 8) && !word(indirect, 12), "GPU replay indirect arguments");
}
static void render(id<MTLCommandBuffer> command, id<MTLLibrary> lib, Buffer rows, Buffer map, Buffer indirect,
                    id<MTLTexture> texture, Buffer pixels) {
    auto fragment = library(@"#include <metal_stdlib>\nusing namespace metal;\nstruct In { float3 color [[user(locn0)]]; };\nfragment float4 tf_fragment(In in [[stage_in]]) { return float4(in.color,1); }\n");
    auto desc = [MTLRenderPipelineDescriptor new];desc.vertexFunction = [lib newFunctionWithName:@"trames_tf_replay"];
    desc.fragmentFunction = [fragment newFunctionWithName:@"tf_fragment"];
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
    NSError* error = nil;auto pipe = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!pipe) throw std::runtime_error(error.description.UTF8String ?: "replay pipeline failed");
    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture = texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    auto encoder = [command renderCommandEncoderWithDescriptor:pass];[encoder setRenderPipelineState:pipe];
    [encoder setVertexBuffer:rows.object offset:rows.offset atIndex:0];[encoder setVertexBuffer:map.object offset:map.offset atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle indirectBuffer:indirect.object indirectBufferOffset:indirect.offset];
    [encoder endEncoding];
    auto blit = [command blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(32,32,1) toBuffer:pixels.object destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:8192];
    [blit endEncoding];
}

int main(int argc, char** argv) { @autoreleasepool { try {
    check(argc == 3, "usage: native-tf-metal generated-directory assembly.metal");
    device = MTLCreateSystemDefaultDevice();check(device != nil, "A real Metal GPU is required");queue = [device newCommandQueue];
    auto dir = [NSString stringWithUTF8String:argv[1]];
    NSString* source = [contents([dir stringByAppendingPathComponent:@"vulkan-native-tf.metal"]) stringByAppendingString:contents([NSString stringWithUTF8String:argv[2]])];
    auto lib = library(source);
    NSDictionary* meta = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:@"vulkan-native-tf.json"]] options:0 error:nil];
    check([meta[@"buffers"] unsignedIntValue] == 3 && [meta[@"scalars"] unsignedIntValue] == 13, "compiler layout metadata");
    std::array<uint32_t,4> strides{{64,16,0,0}};Metal engine(device, lib, strides);
    auto compute = capture_pipeline(lib, meta[@"entry"], 0);
    auto positions = buffer(64*16, 0);
    const float quad[][4] = {{-1,-1,0,1},{1,-1,0,1},{-1,1,0,1},{-1,1,0,1},{1,-1,0,1},{1,1,0,1}};
    std::memcpy(bytes(positions), quad, sizeof(quad));
    struct Case { const char* name;unsigned vertices, instances, topology, capacity, index_type;std::vector<uint32_t> indices, map; };
    std::vector<Case> cases = {
        {"points",5,2,0,8,0,{}, {0,1,2,3,4,5,6,7,8,9}},
        {"line-list",5,2,1,3,0,{}, {0,1,2,3,5,6,7,8}},
        {"line-strip",4,1,2,6,0,{}, {0,1,1,2,2,3}},
        {"triangle-list",7,1,3,6,0,{}, {0,1,2,3,4,5}},
        {"triangle-strip",5,1,4,6,0,{}, {0,1,2,1,3,2,2,3,4}},
        {"triangle-fan",5,1,5,9,0,{}, {1,2,0,2,3,0,3,4,0}},
        {"line-restart16",8,1,2,5,16,{0,1,0xffff,3,4,5,0xffff,7},{0,1,3,4,4,5}},
        {"triangle-restart32",10,2,4,12,32,{0,1,2,3,0xffffffffu,5,6,7,8,9},
          {0,1,2,1,3,2,5,6,7,6,8,7,7,8,9,10,11,12,11,13,12,15,16,17,16,18,17,17,18,19}},
        {"zero",0,1,0,0,0,{},{}},
        {"full",6,1,3,2,0,{}, {0,1,2,3,4,5}},
    };
    NSDictionary* reference = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:
        [dir stringByAppendingPathComponent:@"native-vulkan-reference.json"]] options:0 error:nil];
    check(reference != nil && [reference[@"cases"] count] == 6, "missing independent native Vulkan reference");
    for (NSDictionary* item in reference[@"cases"]) {
        Case test{"native-vulkan-reference",5,2,[item[@"topology"] unsignedIntValue],64,0,{},{}};
        for (NSNumber* value in item[@"map"]) test.map.push_back(value.unsignedIntValue);
        check(test.map.size() == [item[@"vertices"] unsignedIntValue], "native Vulkan reference count");
        cases.push_back(test);
    }
    for (const auto& test : cases) {
        Parameters p{test.vertices,test.instances,test.topology,unsigned(!test.indices.empty()),test.index_type,0,3,
                     std::max(1u,3*test.vertices*test.instances),{test.capacity*64,test.capacity*16,0,0},{64,16,0,0}};
        auto rows = buffer(std::max(1u,p.vertices*p.instances)*512), stats = buffer(65*4,0), state = buffer(sizeof(State));
        auto map = buffer(p.map_capacity*4), draw = buffer(32), indirect = buffer(16), indices = buffer(std::max<size_t>(1,test.indices.size())*4);
        auto b0 = buffer(p.sizes[0]+64), b1 = buffer(p.sizes[1]+16);
        if (p.index_type == 16) for (unsigned i=0;i<test.indices.size();++i) {uint16_t n=uint16_t(test.indices[i]);std::memcpy(bytes(indices)+2*i,&n,2);}
        else if (p.index_type) std::memcpy(bytes(indices),test.indices.data(),test.indices.size()*4);
        auto command = [queue commandBuffer];engine.begin(command,state,{},0);
        // Restart-map tests deliberately isolate primitive assembly. Full
        // indexed capture is separately tested without restart below. The
        // Vulkan adapter still needs to suppress restart vertex invocations.
        capture(command,compute,p,rows,stats,positions);
        engine.assemble(command,p,indices,map,draw,state,indirect);
        engine.scatter(command,p,rows,map,draw,{{b0,b1,{},{}}});commit(command);
        unsigned primitive_size=p.topology==0?1:p.topology<=2?2:3;
        unsigned written=std::min(unsigned(test.map.size()), test.capacity/primitive_size*primitive_size);
        check_draw(draw,map,state,indirect,test.map,written,primitive_size);
        for (unsigned i=0;i<written;++i) check_record(b0,b1,i,test.map[i]%p.vertices,test.map[i]/p.vertices);
        check_holes(b0,b1,written);
        check(word(stats,0)==p.vertices*p.instances,"VS side effects must execute exactly once per input invocation");
        std::printf("native-metal-tf-case-ok: %s written=%u needed=%zu\n",test.name,written,test.map.size());
    }
    for (unsigned index_type : {16u,32u}) {
        auto name=[NSString stringWithFormat:@"vulkan-native-tf-u%u.metal",index_type];
        auto indexed_lib=library([contents([dir stringByAppendingPathComponent:name]) stringByAppendingString:contents([NSString stringWithUTF8String:argv[2]])]);
        auto indexed_compute=capture_pipeline(indexed_lib,meta[@"entry"],index_type);
        Metal indexed_engine(device,indexed_lib,strides);
        Parameters p{5,2,0,0,index_type,2,3,30,{10*64,10*16,0,0},{64,16,0,0}};
        const uint32_t input[]={42,11,9,5,7,5,9};auto indices=buffer(sizeof(input));
        for(unsigned i=0;i<7;++i) {
            if(index_type==16){uint16_t value=uint16_t(input[i]);std::memcpy(bytes(indices)+2*i,&value,2);}
            else std::memcpy(bytes(indices)+4*i,input+i,4);
        }
        auto rows=buffer(10*512),stats=buffer(65*4,0),state=buffer(sizeof(State)),map=buffer(30*4),draw=buffer(32),indirect=buffer(16);
        auto b0=buffer(10*64+64),b1=buffer(10*16+16);auto command=[queue commandBuffer];
        indexed_engine.begin(command,state,{},0);capture(command,indexed_compute,p,rows,stats,positions,indices,-2,7);
        indexed_engine.assemble(command,p,indices,map,draw,state,indirect);
        indexed_engine.scatter(command,p,rows,map,draw,{{b0,b1,{},{}}});commit(command);
        for(unsigned i=0;i<10;++i)check_record(b0,b1,i,input[2+i%5]-2,7+i/5);
        check_holes(b0,b1,10);check(word(stats,0)==10,"indexed shader side effects");
        std::printf("native-metal-tf-indexed-ok: u%u negative base vertex, first index/instance, duplicate indices and exact GPU outputs\n",index_type);
    }
    // GPU-only end/begin counter continuation, non-zero buffer bindings,
    // overflowing a later draw, byte-count conversion and indirect replay.
    auto state=buffer(sizeof(State)), rows=buffer(6*512),stats=buffer(65*4,0),map=buffer(18*4),draw=buffer(32),indirect=buffer(16);
    auto first=buffer(16+5*64+64),second=buffer(16+5*16+16),c0=buffer(16),c1=buffer(16);
    std::array<Buffer,4> counters{{{c0.object,4,4},{c1.object,8,4},{},{}}};
    std::array<Buffer,4> output{{{first.object,16,5*64},{second.object,16,5*16},{},{}}};
    Parameters p{3,1,0,0,0,0,3,18,{5*64,5*16,0,0},{64,16,0,0}};
    auto command=[queue commandBuffer];engine.begin(command,state,{},0);
    capture(command,compute,p,rows,stats,positions,{},2,4);engine.assemble(command,p,{},map,draw,state,indirect);
    engine.scatter(command,p,rows,map,draw,output);engine.end(command,state,counters,3);
    engine.begin(command,state,counters,3);
    capture(command,compute,p,rows,stats,positions,{},6,9);engine.assemble(command,p,{},map,draw,state,indirect);
    engine.scatter(command,p,rows,map,draw,output);engine.end(command,state,counters,3);
    auto byte_indirect=buffer(16);engine.byte_count(command,counters[0],64,64,3,9,byte_indirect);commit(command);
    check(word(c0,4)==5*64 && word(c1,8)==5*16,"GPU pause/resume counter offsets");
    check(word(byte_indirect,0)==4 && word(byte_indirect,4)==3 && word(byte_indirect,12)==9,"GPU byte-count indirect conversion");
    for(unsigned i=0;i<5;++i)check_record(output[0],output[1],i,i<3?2+i:6+i-3,i<3?4:9);
    check_holes(output[0],output[1],5);check(word(stats,0)==6,"resume shader side effect count");
    for(unsigned i=0;i<16;++i)check(bytes(first)[i]==0xa5 && bytes(second)[i]==0xa5,"bound-range leading sentinel");
    for(NSUInteger i=16+5*64;i<first.size;++i)check(bytes(first)[i]==0xa5,"bound-range trailing sentinel");
    std::puts("native-metal-tf-counters-ok: GPU continuation, bounded overflow, base vertex/instance and byte-count");

    // Rasterization reads captured outputs; it never reruns the original VS.
    p={6,1,3,0,0,0,3,18,{6*64,6*16,0,0},{64,16,0,0}};
    auto b0=buffer(6*64+64),b1=buffer(6*16+16);std::memset(bytes(stats),0,stats.size);
    auto texture_desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:32 height:32 mipmapped:NO];
    texture_desc.usage=MTLTextureUsageRenderTarget;texture_desc.storageMode=MTLStorageModePrivate;
    auto texture=[device newTextureWithDescriptor:texture_desc];check(texture!=nil,"replay texture allocation");auto pixels=buffer(8192);
    command=[queue commandBuffer];engine.begin(command,state,{},0);capture(command,compute,p,rows,stats,positions);
    engine.assemble(command,p,{},map,draw,state,indirect);engine.scatter(command,p,rows,map,draw,{{b0,b1,{},{}}});
    render(command,lib,rows,map,indirect,texture,pixels);commit(command);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const uint8_t expected[]={255,64,128,255};
        check(!std::memcmp(bytes(pixels)+y*256+x*4,expected,4),"GPU captured output raster pixel");++pixels_checked;}
    check(word(stats,0)==6,"raster replay must not duplicate SSBO/atomic side effects");
    for(unsigned i=0;i<6;++i)check_record(b0,b1,i,i,0);
    std::printf("native-metal-tf-ok: GPU=%s scalars=%llu map_entries=%llu pixels=%llu; original VS executed once; guest-extension-connected=false\n",
        device.name.UTF8String,(unsigned long long)scalars_checked,(unsigned long long)maps_checked,(unsigned long long)pixels_checked);
    return 0;
} catch (const std::exception& e) {std::fprintf(stderr,"native Metal TF failure: %s\n",e.what());return 1;} } }

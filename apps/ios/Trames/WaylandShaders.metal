// SPDX-License-Identifier: GPL-3.0-or-later
#include <metal_stdlib>
using namespace metal;
struct SceneImage {
    float2 origin;
    float2 size;
    float2 viewport;
    uint offset;
    uint stride;
    uint width;
    uint height;
    uint alpha;
    uint transform;
};
struct Vertex { float4 position [[position]]; float2 uv; };
vertex Vertex waylandVertex(uint id [[vertex_id]], constant SceneImage &image [[buffer(0)]]) {
    constexpr float2 quad[] = {float2(0,0),float2(1,0),float2(0,1),float2(1,1)};
    float2 p = (image.origin + quad[id] * image.size) / image.viewport;
    return {float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1), quad[id]};
}
fragment float4 waylandShm(Vertex vertex [[stage_in]], const device uchar *bytes [[buffer(0)]],
                           constant SceneImage &image [[buffer(1)]]) {
    float2 uv=vertex.uv;
    if (image.transform & 4) uv.x=1-uv.x;
    switch (image.transform & 3) {
      case 1: uv=float2(uv.y,1-uv.x); break;
      case 2: uv=1-uv; break;
      case 3: uv=float2(1-uv.y,uv.x); break;
    }
    uint2 at = min(uint2(uv * float2(image.width, image.height)), uint2(image.width-1,image.height-1));
    uint p = image.offset + at.y * image.stride + at.x * 4;
    return float4(bytes[p+2],bytes[p+1],bytes[p],image.alpha ? bytes[p+3] : 255) / 255.0;
}
fragment float4 waylandTexture(Vertex vertex [[stage_in]], texture2d<float> image [[texture(0)]]) {
    constexpr sampler linear(coord::normalized, filter::linear, address::clamp_to_edge);
    return image.sample(linear,vertex.uv);
}

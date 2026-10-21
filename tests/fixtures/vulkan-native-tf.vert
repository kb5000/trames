#version 450
layout(location=0) in vec4 position;
layout(set=0,binding=0,std430) buffer Stats { uint calls; uint per_vertex[64]; } stats;
layout(location=0,xfb_buffer=0,xfb_offset=4,xfb_stride=64) out vec3 color;
layout(location=0,component=3,xfb_buffer=0,xfb_offset=48,xfb_stride=64) out float packed;
layout(location=1,xfb_buffer=0,xfb_offset=16,xfb_stride=64) flat out uvec2 ids;
layout(location=2,xfb_buffer=0,xfb_offset=24,xfb_stride=64) out mat2 basis;
layout(location=4,xfb_buffer=0,xfb_offset=40,xfb_stride=64) out float values[2];
layout(location=6,xfb_buffer=1,xfb_offset=4,xfb_stride=16) flat out uint bits;
void main() {
    uint n=uint(gl_VertexIndex);
    atomicAdd(stats.calls,1);
    atomicAdd(stats.per_vertex[n],1);
    color=vec3(1.0,0.25,0.5);
    packed=float(n)+4000.0;
    ids=uvec2(n,uint(gl_InstanceIndex));
    basis=mat2(vec2(float(n),float(n)+10.0),vec2(float(n)+20.0,float(n)+30.0));
    values[0]=float(n)+1000.0; values[1]=float(n)+2000.0;
    bits=0x7fc12345u+n;
    gl_Position=position;
    gl_PointSize=1.0;
}

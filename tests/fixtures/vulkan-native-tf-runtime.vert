// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec4 position;
layout(set=0,binding=0,std430) buffer Stats { uint calls; } stats;
layout(location=0,xfb_buffer=0,xfb_offset=4,xfb_stride=24) out vec4 captured;
layout(location=1,xfb_buffer=1,xfb_offset=0,xfb_stride=8) out float other;
layout(location=2) out vec4 color;
void main() {
    atomicAdd(stats.calls,1);
    float n=float(gl_VertexIndex);
    captured=vec4(n,float(gl_InstanceIndex),n*3+1,17);
    other=n+100;
    color=vec4(0.25,0.5,0.75,1);
    gl_Position=position;
}

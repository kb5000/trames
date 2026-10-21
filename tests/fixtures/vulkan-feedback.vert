// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0, xfb_buffer=0, xfb_offset=4, xfb_stride=24) out vec4 captured;
layout(location=1, xfb_buffer=1, xfb_offset=0, xfb_stride=8) out float other;
void main() {
    float n=float(gl_VertexIndex);
    captured=vec4(n,float(gl_InstanceIndex),n*3+1,17);
    other=n+100;
    gl_Position=vec4(0,0,0,1);
    gl_PointSize=1;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0,component=0) out vec2 lower;
layout(location=0,component=2) out vec2 upper;
void main() {
    vec2 positions[3]=vec2[](vec2(-0.8,-0.8),vec2(0.8,-0.8),vec2(0.0,0.8));
    vec3 colors[3]=vec3[](vec3(1,0,0),vec3(0,1,0),vec3(0,0,1));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
    lower=colors[gl_VertexIndex].rg;
    upper=vec2(colors[gl_VertexIndex].b,1);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(push_constant) uniform State { float depth; } state;
void main() {
    vec2 p[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    int i=gl_VertexIndex%3;
    if(gl_VertexIndex>=3)i=2-i;
    gl_Position=vec4(p[i],state.depth,1);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(binding=0) uniform sampler2DShadow shadow;
layout(binding=1) uniform usampler2D stencil;
layout(location=0) out vec4 pixel;
void main() { pixel=vec4(texture(shadow,vec3(0.5,0.5,0.5)),float(texture(stencil,vec2(0.5)).r)/255.0,0,1); }

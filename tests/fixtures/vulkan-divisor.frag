// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) flat in vec3 rgb;
layout(location=0) out vec4 pixel;
void main() { pixel=vec4(rgb,1); }

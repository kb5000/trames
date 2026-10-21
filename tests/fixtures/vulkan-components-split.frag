// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0,component=0) in vec2 lower;
layout(location=0,component=2) in vec2 upper;
layout(location=0) out vec4 pixel;
void main() { pixel=vec4(lower,upper); }

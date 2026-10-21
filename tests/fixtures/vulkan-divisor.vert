// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 corner;
layout(location=1) in vec4 color;
layout(location=2) in float tone;
layout(location=0) flat out vec3 rgb;
void main() {
    gl_Position=vec4((corner.x+1.)/8.-1.+float(gl_InstanceIndex)/4.,corner.y,0,1);
    rgb=vec3(color.rg,tone);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec2 instanceOffset;
layout(location=0) out vec2 uv;
layout(set=0,binding=4) uniform Transform { vec4 transform; } params;
layout(push_constant) uniform Controls { vec4 controls; } push;
void main() { gl_Position=vec4(position*params.transform.xy+instanceOffset+push.controls.xy,0,1); uv=texcoord; }

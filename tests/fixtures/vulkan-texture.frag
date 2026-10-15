// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0) uniform sampler2D images[2];
layout(set=0,binding=1) uniform texture2D separateImage;
layout(set=0,binding=2) uniform sampler separateSampler;
layout(set=0,binding=3) uniform Tint { vec4 tint; } params;
layout(push_constant) uniform Controls { vec4 controls; } push;
void main() { color=(texture(images[0],uv)+texture(images[1],uv)+texture(sampler2D(separateImage,separateSampler),uv))/3.0*params.tint*vec4(push.controls.zw,1,1); }

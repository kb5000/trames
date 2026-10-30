#version 450
#extension GL_EXT_multiview : require
layout(location=0) out vec4 color;
void main(){color=vec4(float(gl_ViewIndex+1)/4,0,0,1);gl_FragDepth=float(gl_SampleID+1)/8;}

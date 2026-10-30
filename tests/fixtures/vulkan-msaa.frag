#version 450
layout(push_constant) uniform Push { vec4 color; uint per_sample; } pc;
layout(location=0) out vec4 output_color;
void main(){output_color=pc.color;if(pc.per_sample!=0)output_color.r=float(gl_SampleID&1);}

#version 450
#extension GL_EXT_multiview : require
vec2 positions[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
void main(){gl_Position=vec4(positions[gl_VertexIndex],0.5,1);}

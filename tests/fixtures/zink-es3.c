// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary original-Mesa client. No version or extension overrides.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "zink-es3: %s failed (EGL=0x%x GL=0x%x)\n", what, eglGetError(), glGetError());
        exit(1);
    }
}
static GLuint shader(GLenum type, const char *source) {
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if (!ok) { char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log); }
    require(ok,"compile shader");return s;
}
static GLuint program(int es3, int feedback) {
    const char *vertex=es3 ?
        "#version 300 es\nin vec2 position; out vec4 captured; void main(){gl_Position=vec4(position,0.,1.);captured=vec4(position,2.,1.);}" :
        "attribute vec2 position; void main(){gl_Position=vec4(position,0.,1.);}";
    const char *fragment=es3 ?
        "#version 300 es\nprecision mediump float;layout(location=0)out vec4 a;layout(location=1)out vec4 b;layout(location=2)out vec4 c;layout(location=3)out vec4 d;uniform float gain;void main(){a=vec4(1,0,0,1);b=vec4(0,1,0,1);c=vec4(0,0,1,1);d=vec4(1,1,0,1);a.rgb*=gain;b.rgb*=gain;c.rgb*=gain;d.rgb*=gain;}" :
        "#extension GL_EXT_draw_buffers : require\nprecision mediump float;uniform float gain;void main(){gl_FragData[0]=vec4(1,0,0,1);gl_FragData[1]=vec4(0,1,0,1);gl_FragData[2]=vec4(0,0,1,1);gl_FragData[3]=vec4(1,1,0,1);gl_FragData[0].rgb*=gain;gl_FragData[1].rgb*=gain;gl_FragData[2].rgb*=gain;gl_FragData[3].rgb*=gain;}";
    GLuint v=shader(GL_VERTEX_SHADER,vertex),f=shader(GL_FRAGMENT_SHADER,fragment),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,f);glBindAttribLocation(p,0,"position");
    if(feedback){const char *name="captured";glTransformFeedbackVaryings(p,1,&name,GL_INTERLEAVED_ATTRIBS);}
    glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    require(ok,"link program");glDeleteShader(v);glDeleteShader(f);return p;
}
static void draw(int es3) {
    GLint attachments=0;glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&attachments);
    printf("GL_MAX_COLOR_ATTACHMENTS=%d\n",attachments);fflush(stdout);
    require(attachments>=4,"four color attachments");
    const GLfloat vertices[]={-1,-1,3,-1,-1,3};GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    GLuint p=program(es3,es3);glUseProgram(p);glUniform1f(glGetUniformLocation(p,"gain"),1.0f);
    GLuint fb,textures[4];GLenum targets[4];glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenTextures(4,textures);
    for(unsigned i=0;i<4;i++){
        targets[i]=GL_COLOR_ATTACHMENT0+i;glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER,targets[i],GL_TEXTURE_2D,textures[i],0);
    }
    typedef void (*DrawBuffers)(GLsizei,const GLenum *);
    DrawBuffers buffers=es3 ? glDrawBuffers : (DrawBuffers)eglGetProcAddress("glDrawBuffersEXT");
    require(buffers!=NULL,"draw buffers entrypoint");buffers(4,targets);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"MRT framebuffer");
    glViewport(0,0,8,8);glDrawArrays(GL_TRIANGLES,0,3);glFinish();require(glGetError()==GL_NO_ERROR,"MRT draw");
    GLuint reader;glGenFramebuffers(1,&reader);glBindFramebuffer(GL_FRAMEBUFFER,reader);
    unsigned char expected[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
    for(unsigned i=0;i<4;i++){
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
        unsigned char pixels[8*8*4];glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        require(glGetError()==GL_NO_ERROR,"read MRT attachment");
        for(unsigned j=0;j<sizeof(pixels);j++)require(pixels[j]==expected[i][j%4],"MRT pixel");
    }
    glBindFramebuffer(GL_FRAMEBUFFER,fb);const GLenum sparse[]={GL_NONE,GL_NONE,GL_COLOR_ATTACHMENT2,GL_NONE};buffers(4,sparse);
    glUniform1f(glGetUniformLocation(p,"gain"),0.5f);glDrawArrays(GL_TRIANGLES,0,3);glFinish();require(glGetError()==GL_NO_ERROR,"sparse MRT draw");
    glBindFramebuffer(GL_FRAMEBUFFER,reader);expected[2][2]=128;
    for(unsigned i=0;i<4;i++){
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
        unsigned char pixels[8*8*4];glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        require(glGetError()==GL_NO_ERROR,"read sparse MRT attachment");
        for(unsigned j=0;j<sizeof(pixels);j++)require(abs((int)pixels[j]-expected[i][j%4])<=1,"sparse MRT pixel and preserved attachments");
    }
    puts("zink-mrt-ok: dense/sparse shader outputs / 512 pixels and preserved disabled attachments correct");fflush(stdout);
    if(es3){
        GLuint output;glGenBuffers(1,&output);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,output);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,12*sizeof(float),NULL,GL_STATIC_READ);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,output);glEnable(GL_RASTERIZER_DISCARD);
        glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,3);glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);glFinish();
        require(glGetError()==GL_NO_ERROR,"transform feedback draw");
        const float *values=glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER,0,12*sizeof(float),GL_MAP_READ_BIT);require(values!=NULL,"map transform feedback");
        for(unsigned i=0;i<3;i++){require(values[i*4]==vertices[i*2] && values[i*4+1]==vertices[i*2+1] && values[i*4+2]==2 && values[i*4+3]==1,"transform feedback values");}
        require(glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER),"unmap transform feedback");glDeleteBuffers(1,&output);
        puts("zink-es3-feedback-ok: 12 GPU transform feedback scalars correct");
    }
    glDeleteFramebuffers(1,&reader);glDeleteFramebuffers(1,&fb);glDeleteTextures(4,textures);glDeleteProgram(p);glDeleteBuffers(1,&buffer);
}
static GLuint texture_program(const char *vertex,const char *fragment) {
    GLuint v=shader(GL_VERTEX_SHADER,vertex?vertex:"attribute vec2 position;varying vec2 uv;void main(){gl_Position=vec4(position,0.,1.);uv=(position+1.)*.5;}"),f=shader(GL_FRAGMENT_SHADER,fragment),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,f);glBindAttribLocation(p,0,"position");glLinkProgram(p);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);require(ok,"texture program");glDeleteShader(v);glDeleteShader(f);return p;
}
static void texture_pixel(GLuint p,unsigned size,const unsigned char expected[4]) {
    glUseProgram(p);glViewport(0,0,size,size);glDrawArrays(GL_TRIANGLES,0,3);glFinish();
    unsigned char got[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,got);require(glGetError()==GL_NO_ERROR,"sample texture");
    if(memcmp(got,expected,4))fprintf(stderr,"texture pixel: got %u %u %u %u expected %u %u %u %u\n",got[0],got[1],got[2],got[3],expected[0],expected[1],expected[2],expected[3]);
    for(unsigned i=0;i<4;i++)require(abs((int)got[i]-expected[i])<=1,"texture texel");
}
static void textures(void) {
    const GLfloat vertices[]={-1,-1,3,-1,-1,3};GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    GLuint flat=texture_program(NULL,"precision mediump float;varying vec2 uv;uniform sampler2D source;void main(){gl_FragColor=texture2D(source,uv);}");
    GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    const unsigned char colors[3][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255}};
    unsigned char pixels[64];
    for(unsigned level=0;level<3;level++){
        unsigned n=4>>level;for(unsigned i=0;i<n*n;i++)memcpy(pixels+i*4,colors[level],4);
        glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA,n,n,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    texture_pixel(flat,8,colors[0]);texture_pixel(flat,1,colors[2]);
    glGenerateMipmap(GL_TEXTURE_2D);require(glGetError()==GL_NO_ERROR,"generate 2D mipmaps");texture_pixel(flat,1,colors[0]);
    glDeleteTextures(1,&tex);glDeleteProgram(flat);
    puts("zink-textures-2d-ok: uploaded mip selection and regenerated 2D mip levels");fflush(stdout);
    // Explicit texture LOD is core GLSL ES 1.00 in the vertex stage; the
    // fragment-stage EXT_shader_texture_lod extension need not be advertised.
    GLint units=0;glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,&units);require(units>0,"vertex texture sampling");
    GLuint cube=texture_program("attribute vec2 position;uniform samplerCube source;uniform vec3 direction;varying mediump vec4 color;void main(){gl_Position=vec4(position,0.,1.);color=textureCubeLod(source,direction,2.);}",
        "precision mediump float;varying mediump vec4 color;void main(){gl_FragColor=color;}");
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_CUBE_MAP,tex);
    const unsigned char faces[6][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{255,0,255,255},{0,255,255,255}};
    const GLfloat directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(unsigned face=0;face<6;face++)for(unsigned level=0;level<3;level++){
        unsigned n=4>>level;for(unsigned i=0;i<n*n;i++)memcpy(pixels+i*4,level?colors[0]:faces[face],4);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,GL_RGBA,n,n,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);require(glGetError()==GL_NO_ERROR,"generate cube mipmaps");
    glUseProgram(cube);GLint direction=glGetUniformLocation(cube,"direction");require(direction>=0,"cube direction");
    for(unsigned face=0;face<6;face++){glUniform3fv(direction,1,directions[face]);texture_pixel(cube,1,faces[face]);}
    glDeleteTextures(1,&tex);glDeleteProgram(cube);glDeleteBuffers(1,&buffer);
    puts("zink-textures-ok: distinct uploaded 2D mip levels, regenerated mips and all six cube faces sampled at LOD2 on GPU");fflush(stdout);
}
int main(int argc, char **argv) {
    int texture_only=argc==2 && !strcmp(argv[1],"--textures");
    int es3=!(texture_only || (argc==2 && !strcmp(argv[1],"--mrt")));
    PFNEGLGETPLATFORMDISPLAYEXTPROC get=(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");require(get!=NULL,"platform display entrypoint");
    EGLDisplay display=get(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);EGLint major,minor;
    require(eglInitialize(display,&major,&minor),"initialize EGL");require(eglBindAPI(EGL_OPENGL_ES_API),"bind GLES");
    const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,es3?EGL_OPENGL_ES3_BIT_KHR:EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;require(eglChooseConfig(display,attributes,&config,1,&count),"choose config");
    printf("EGL %d.%d: GLES%d configs=%d\n",major,minor,es3?3:2,count);fflush(stdout);require(count==1,"requested GLES config");
    const EGLint size[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE},version[]={EGL_CONTEXT_CLIENT_VERSION,es3?3:2,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,size);EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,version);
    require(context!=EGL_NO_CONTEXT && surface!=EGL_NO_SURFACE,"create context and surface");require(eglMakeCurrent(display,surface,surface,context),"make current");
    const char *renderer=(const char *)glGetString(GL_RENDERER);require(renderer && strstr(renderer,"zink"),"original Zink renderer");
    printf("GL_VERSION=%s\nGL_RENDERER=%s\n",glGetString(GL_VERSION),renderer);fflush(stdout);
    if(es3){GLint version_major=0;glGetIntegerv(GL_MAJOR_VERSION,&version_major);require(version_major>=3,"real GLES3 version");}
    if(texture_only)textures();else draw(es3);require(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT),"release context");
    require(eglDestroyContext(display,context) && eglDestroySurface(display,surface) && eglTerminate(display),"teardown");
    puts(texture_only?"zink-es2-textures-ok":es3?"zink-es3-ok":"zink-es2-mrt-ok");return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Stock FFmpeg/libva -> immutable native FDs -> fork/SCM_RIGHTS -> native Vulkan.
// The explicit CPU readback is a test oracle, never part of the rendering path.
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <va/va.h>
#include <va/va_drmcommon.h>
#include <vulkan/vulkan.h>
#ifdef TEST_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#endif
#define VK_OK(e) do { VkResult result=(e); if(result!=VK_SUCCESS) {fprintf(stderr,"%s = %d at %d\n",#e,result,__LINE__);abort();} } while(0)
#define AV_OK(e) do { int result=(e); if(result<0) {char error[128];av_strerror(result,error,sizeof(error));fprintf(stderr,"%s = %s at %d\n",#e,error,__LINE__);abort();} } while(0)
#define VA_OK(e) do { VAStatus result=(e); if(result!=VA_STATUS_SUCCESS) {fprintf(stderr,"%s = %s at %d\n",#e,vaErrorStr(result),__LINE__);abort();} } while(0)
static enum AVPixelFormat select_vaapi(AVCodecContext *ctx,const enum AVPixelFormat *formats) {
    (void)ctx;for(;*formats!=AV_PIX_FMT_NONE;formats++)if(*formats==AV_PIX_FMT_VAAPI)return *formats;
    abort();
}
#ifndef TEST_EGL
static uint32_t memory_type(VkPhysicalDevice physical,uint32_t bits,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props;vkGetPhysicalDeviceMemoryProperties(physical,&props);
    for(uint32_t i=0;i<props.memoryTypeCount;i++)if((bits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&flags)==flags)return i;
    abort();
}
#endif
static void send_fds(int socket,const int fds[2]) {
    char byte=1,control[CMSG_SPACE(2*sizeof(int))]={0};struct iovec io={&byte,1};
    struct msghdr msg={.msg_iov=&io,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
    struct cmsghdr *header=CMSG_FIRSTHDR(&msg);header->cmsg_level=SOL_SOCKET;header->cmsg_type=SCM_RIGHTS;header->cmsg_len=CMSG_LEN(2*sizeof(int));memcpy(CMSG_DATA(header),fds,2*sizeof(int));
    assert(sendmsg(socket,&msg,0)==1);
}
static void receive_fds(int socket,int fds[2]) {
    char byte=0,control[CMSG_SPACE(2*sizeof(int))]={0};struct iovec io={&byte,1};
    struct msghdr msg={.msg_iov=&io,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
    assert(recvmsg(socket,&msg,MSG_CMSG_CLOEXEC)==1 && byte==1 && !(msg.msg_flags&MSG_CTRUNC));
    struct cmsghdr *header=CMSG_FIRSTHDR(&msg);assert(header && header->cmsg_level==SOL_SOCKET && header->cmsg_type==SCM_RIGHTS && header->cmsg_len==CMSG_LEN(2*sizeof(int)));memcpy(fds,CMSG_DATA(header),2*sizeof(int));
}
#ifndef TEST_EGL
static void sample_vulkan(const VADRMPRIMESurfaceDescriptor *desc,int fds[2],const uint8_t *expected) {
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_2};
    VkInstanceCreateInfo instance_info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};VkInstance instance;VK_OK(vkCreateInstance(&instance_info,NULL,&instance));
    uint32_t count=1;VkPhysicalDevice physical;VK_OK(vkEnumeratePhysicalDevices(instance,&count,&physical));
    uint32_t family_count=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,NULL);assert(family_count);
    VkQueueFamilyProperties *families=calloc(family_count,sizeof(*families));assert(families);vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,families);uint32_t family=0;while(family<family_count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<family_count);free(families);
    float priority=1;VkDeviceQueueCreateInfo queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *extensions[]={VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue_info,.enabledExtensionCount=5,.ppEnabledExtensionNames=extensions};VkDevice device;VK_OK(vkCreateDevice(physical,&device_info,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    PFN_vkGetMemoryFdPropertiesKHR fd_properties=(PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(device,"vkGetMemoryFdPropertiesKHR");assert(fd_properties);
    VkCommandPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};VkCommandPool pool;VK_OK(vkCreateCommandPool(device,&pool_info,NULL,&pool));
    VkCommandBufferAllocateInfo command_info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;VK_OK(vkAllocateCommandBuffers(device,&command_info,&command));
    for(unsigned plane=0;plane<2;plane++) {
        uint32_t width=desc->width>>(plane!=0),height=desc->height>>(plane!=0),bytes=plane?2:1;
        VkFormat format=plane?VK_FORMAT_R8G8_UNORM:VK_FORMAT_R8_UNORM;
        VkPhysicalDeviceExternalImageFormatInfo ext_query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
        VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,.pNext=&ext_query,.drmFormatModifier=0,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        VkPhysicalDeviceImageFormatInfo2 query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,.pNext=&mod_query,.format=format,.type=VK_IMAGE_TYPE_2D,.tiling=VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
        VkExternalImageFormatProperties external_properties={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};VkImageFormatProperties2 properties={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&external_properties};VK_OK(vkGetPhysicalDeviceImageFormatProperties2(physical,&query,&properties));
        assert(external_properties.externalMemoryProperties.externalMemoryFeatures==(VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT|VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT));
        struct stat statbuf;assert(!fstat(fds[plane],&statbuf) && statbuf.st_size==desc->objects[plane].size);
        VkSubresourceLayout plane_layout={.offset=desc->layers[plane].offset[0],.rowPitch=desc->layers[plane].pitch[0]};
        VkExternalMemoryImageCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
        VkImageDrmFormatModifierExplicitCreateInfoEXT modifier={.sType=VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,.pNext=&external,.drmFormatModifier=0,.drmFormatModifierPlaneCount=1,.pPlaneLayouts=&plane_layout};
        VkImageCreateInfo create={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=&modifier,.imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,.usage=query.usage,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};VkImage image;VK_OK(vkCreateImage(device,&create,NULL,&image));
        VkMemoryRequirements req;vkGetImageMemoryRequirements(device,image,&req);
        VkMemoryFdPropertiesKHR fd_props={.sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};VK_OK(fd_properties(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,fds[plane],&fd_props));assert(req.memoryTypeBits&fd_props.memoryTypeBits);
        VkMemoryDedicatedAllocateInfo dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.image=image};
        VkImportMemoryFdInfoKHR import={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,.pNext=&dedicated,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,.fd=fds[plane]};VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,.allocationSize=req.size,.memoryTypeIndex=memory_type(physical,req.memoryTypeBits&fd_props.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};VkDeviceMemory memory;VK_OK(vkAllocateMemory(device,&allocation,NULL,&memory));assert(fcntl(fds[plane],F_GETFD)==-1);VK_OK(vkBindImageMemory(device,image,memory,0));
        VkBufferCreateInfo buffer_info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=(uint64_t)width*height*bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};VkBuffer buffer;VK_OK(vkCreateBuffer(device,&buffer_info,NULL,&buffer));vkGetBufferMemoryRequirements(device,buffer,&req);allocation=(VkMemoryAllocateInfo){.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(physical,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};VkDeviceMemory readback;VK_OK(vkAllocateMemory(device,&allocation,NULL,&readback));VK_OK(vkBindBufferMemory(device,buffer,readback,0));void *mapped;VK_OK(vkMapMemory(device,readback,0,VK_WHOLE_SIZE,0,&mapped));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK_OK(vkBeginCommandBuffer(command,&begin));
        // Export waits for the actual VideoToolbox callback. Acquire the immutable
        // external contents using GENERAL, never an UNDEFINED discard transition.
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,.dstQueueFamilyIndex=family,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy region={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
        VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);VK_OK(vkEndCommandBuffer(command));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};VK_OK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));VK_OK(vkQueueWaitIdle(queue));
        assert(!memcmp(mapped,expected,(size_t)width*height*bytes));expected+=(size_t)width*height*bytes;
        vkUnmapMemory(device,readback);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,readback,NULL);vkDestroyImage(device,image,NULL);vkFreeMemory(device,memory,NULL);VK_OK(vkResetCommandPool(device,pool,0));
    }
    vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
}
#endif
#ifdef TEST_EGL
static void sample_egl(const VADRMPRIMESurfaceDescriptor *desc,int fds[2],const uint8_t *expected) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");assert(platform);
    EGLDisplay display=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);assert(display!=EGL_NO_DISPLAY);assert(eglInitialize(display,NULL,NULL));
    const char *extensions=eglQueryString(display,EGL_EXTENSIONS);assert(extensions && strstr(extensions,"EGL_EXT_image_dma_buf_import"));
    // Import-only images must not accidentally enable the export extension.
    assert(!strstr(extensions,"EGL_MESA_image_dma_buf_export"));
    assert(eglBindAPI(EGL_OPENGL_ES_API));EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};EGLConfig config;EGLint count;assert(eglChooseConfig(display,config_attrs,&config,1,&count) && count);
    EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);assert(context!=EGL_NO_CONTEXT && eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
    PFNEGLCREATEIMAGEKHRPROC create=(void*)eglGetProcAddress("eglCreateImageKHR");PFNEGLDESTROYIMAGEKHRPROC destroy=(void*)eglGetProcAddress("eglDestroyImageKHR");PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target=(void*)eglGetProcAddress("glEGLImageTargetTexture2DOES");assert(create && destroy && target);
    fprintf(stderr,"EGL renderer: %s\n",glGetString(GL_RENDERER));assert(strstr((const char*)glGetString(GL_RENDERER),"MOLTENVK"));
    for(unsigned plane=0;plane<2;plane++) {
        uint32_t width=desc->width>>(plane!=0),height=desc->height>>(plane!=0);
        EGLint attrs[]={EGL_WIDTH,(EGLint)width,EGL_HEIGHT,(EGLint)height,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)desc->layers[plane].drm_format,EGL_DMA_BUF_PLANE0_FD_EXT,fds[plane],EGL_DMA_BUF_PLANE0_OFFSET_EXT,0,EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)desc->layers[plane].pitch[0],EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,0,EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,0,EGL_NONE};
        EGLImageKHR image=create(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,attrs);if(image==EGL_NO_IMAGE_KHR){fprintf(stderr,"eglCreateImageKHR failed: %x\n",eglGetError());abort();}
        GLuint texture,framebuffer;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);target(GL_TEXTURE_2D,image);assert(glGetError()==GL_NO_ERROR);glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        uint8_t *rgba=malloc((size_t)width*height*4);assert(rgba);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba);assert(glGetError()==GL_NO_ERROR);
        for(size_t pixel=0;pixel<(size_t)width*height;pixel++){assert(rgba[pixel*4]==expected[pixel*(plane?2:1)]);if(plane)assert(rgba[pixel*4+1]==expected[pixel*2+1]);}
        expected+=(size_t)width*height*(plane?2:1);free(rgba);glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&texture);assert(destroy(display,image));close(fds[plane]);
    }
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));assert(eglDestroyContext(display,context));assert(eglTerminate(display));
}
#endif
int main(int argc,char **argv) {
    assert(argc==2);av_log_set_level(AV_LOG_WARNING);
    AVBufferRef *hardware=NULL;AV_OK(av_hwdevice_ctx_create(&hardware,AV_HWDEVICE_TYPE_VAAPI,"/dev/dri/renderD128",NULL,0));
    AVFormatContext *input=NULL;AV_OK(avformat_open_input(&input,argv[1],NULL,NULL));AV_OK(avformat_find_stream_info(input,NULL));int stream=av_find_best_stream(input,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);AV_OK(stream);
    AVCodecContext *codec=avcodec_alloc_context3(avcodec_find_decoder(input->streams[stream]->codecpar->codec_id));assert(codec);AV_OK(avcodec_parameters_to_context(codec,input->streams[stream]->codecpar));codec->get_format=select_vaapi;codec->hw_device_ctx=av_buffer_ref(hardware);AV_OK(avcodec_open2(codec,codec->codec,NULL));
    AVPacket *packet=av_packet_alloc();AVFrame *frame=av_frame_alloc(),*reference=av_frame_alloc();assert(packet && frame && reference);int ready=0;
    while(!ready) {int next=av_read_frame(input,packet);if(next==AVERROR_EOF){AV_OK(avcodec_send_packet(codec,NULL));}else{AV_OK(next);if(packet->stream_index!=stream){av_packet_unref(packet);continue;}AV_OK(avcodec_send_packet(codec,packet));av_packet_unref(packet);}int status=avcodec_receive_frame(codec,frame);if(status==0)ready=1;else assert(status==AVERROR(EAGAIN));}
    assert(frame->format==AV_PIX_FMT_VAAPI);AVHWDeviceContext *hw=(void*)hardware->data;AVVAAPIDeviceContext *va=hw->hwctx;VASurfaceID surface=(uintptr_t)frame->data[3];VADRMPRIMESurfaceDescriptor desc={0};
    // Firefox exports before syncing, so exercise precisely that order.
    VA_OK(vaExportSurfaceHandle(va->display,surface,VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,VA_EXPORT_SURFACE_READ_ONLY|VA_EXPORT_SURFACE_SEPARATE_LAYERS,&desc));VA_OK(vaSyncSurface(va->display,surface));assert(desc.num_objects==2 && desc.num_layers==2);
    AV_OK(av_hwframe_transfer_data(reference,frame,0));assert(reference->format==AV_PIX_FMT_NV12);size_t size=(size_t)desc.width*desc.height*3/2;uint8_t *expected=malloc(size);assert(expected);size_t offset=0;
    for(unsigned plane=0;plane<2;plane++)for(unsigned row=0;row<(desc.height>>(plane!=0));row++){memcpy(expected+offset,reference->data[plane]+row*reference->linesize[plane],desc.width);offset+=desc.width;}
    int sockets[2],fds[2]={desc.objects[0].fd,desc.objects[1].fd};assert(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,sockets));pid_t child=fork();assert(child>=0);
    if(!child) {close(sockets[0]);close(fds[0]);close(fds[1]);receive_fds(sockets[1],fds);
#ifdef TEST_EGL
    sample_egl(&desc,fds,expected);
#else
    sample_vulkan(&desc,fds,expected);
#endif
    close(sockets[1]);free(expected);puts("vaapi-native-import-ok: hardware H.264, export-before-sync, fork/SCM_RIGHTS, native GPU Y/UV pixels match software-visible VA frame");fflush(stdout);_exit(0);}
    close(sockets[1]);send_fds(sockets[0],fds);close(fds[0]);close(fds[1]);av_frame_free(&reference);av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&codec);avformat_close_input(&input);av_buffer_unref(&hardware);free(expected);close(sockets[0]);int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);return 0;
}

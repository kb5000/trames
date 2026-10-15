// SPDX-License-Identifier: GPL-3.0-or-later
// Identical native/guest application. GPU rasterization is checked against
// independent barycentric equations, including clear/scissor and repeated draws.
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define OK(call) do {VkResult r=(call);if(r!=VK_SUCCESS){fprintf(stderr,"%s: %d at %d\n",#call,r,__LINE__);abort();}}while(0)
#define W 256
#define H 192
static VkShaderModule shader(VkDevice d,const char *path) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n%4==0);rewind(f);
    uint32_t *code=malloc((size_t)n);assert(code && fread(code,1,(size_t)n,f)==(size_t)n);fclose(f);
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)n,.pCode=code};VkShaderModule out;OK(vkCreateShaderModule(d,&info,NULL,&out));free(code);return out;
}
static uint32_t type(VkPhysicalDevice physical,uint32_t bits,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p;vkGetPhysicalDeviceMemoryProperties(physical,&p);
    for(uint32_t i=0;i<p.memoryTypeCount;i++)if((bits&(1u<<i)) && (p.memoryTypes[i].propertyFlags&flags)==flags)return i;
    abort();
}
static void pixels(const uint8_t *rgba,unsigned round) {
    unsigned checked=0,colored=0;
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
        const uint8_t *p=rgba+(y*W+x)*4;
        double px=2.0*(x+0.5)/W-1.0,py=2.0*(y+0.5)/H-1.0;
        double blue=(py+0.8)/1.6,green=((px/0.8)+1.0-blue)/2.0,red=1.0-blue-green;
        int clipped=round==1 && x<W/2;
        int inside=red>0.02 && green>0.02 && blue>0.02;
        int outside=red< -0.02 || green< -0.02 || blue< -0.02;
        if(!clipped && !inside && !outside)continue;
        const double background[3]={8,12,20},color[3]={red*255,green*255,blue*255};
        for(unsigned c=0;c<3;c++){
            double wanted=!clipped && inside?color[c]:background[c];
            if(fabs(p[c]-wanted)>2.0){fprintf(stderr,"pixel %u,%u channel %u got %u expected %.3f round %u\n",x,y,c,p[c],wanted,round);abort();}
        }
        assert(p[3]==255);checked++;if(!clipped && inside)colored++;
    }
    assert(checked>W*H*9/10 && colored>W*H/10);
    printf("render round %u: barycentric colors, alpha, background and scissor correct\n",round+1);
}
int main(int argc,char **argv) {
    assert(argc==4 || argc==5);int bridge=argc==5;
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VkInstance instance;OK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t n=1;VkPhysicalDevice physical;OK(vkEnumeratePhysicalDevices(instance,&n,&physical));assert(n==1);
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);fprintf(stderr,"Vulkan render driver: %s\n",properties.deviceName);
    VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,&fp);assert(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    VkImageFormatProperties imageProperties;OK(vkGetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,0,&imageProperties));assert(imageProperties.maxExtent.width>=W && imageProperties.maxExtent.height>=H);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,NULL);VkQueueFamilyProperties *families=calloc(n,sizeof(*families));assert(families);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families);uint32_t family=0;while(family<n && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<n);free(families);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;OK(vkCreateDevice(physical,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkImageCreateInfo imageInfo={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={W,H,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};VkImage image;
    if(bridge){VkImageCreateInfo bad=imageInfo;bad.extent.width=0;assert(vkCreateImage(device,&bad,NULL,&image)!=VK_SUCCESS);bad=imageInfo;bad.format=VK_FORMAT_D32_SFLOAT;assert(vkCreateImage(device,&bad,NULL,&image)==VK_ERROR_FORMAT_NOT_SUPPORTED);}
    OK(vkCreateImage(device,&imageInfo,NULL,&image));VkMemoryRequirements imageReq;vkGetImageMemoryRequirements(device,image,&imageReq);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=imageReq.size,.memoryTypeIndex=type(physical,imageReq.memoryTypeBits,0)};VkDeviceMemory imageMemory;OK(vkAllocateMemory(device,&alloc,NULL,&imageMemory));
    if(bridge)assert(vkBindImageMemory(device,image,imageMemory,imageReq.size)==VK_ERROR_UNKNOWN);
    OK(vkBindImageMemory(device,image,imageMemory,0));
    VkImageViewCreateInfo viewInfo={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};VkImageView view;OK(vkCreateImageView(device,&viewInfo,NULL,&view));
    VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};VkSubpassDependency dependency={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};VkRenderPassCreateInfo passInfo={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=1,.pDependencies=&dependency};VkRenderPass pass;OK(vkCreateRenderPass(device,&passInfo,NULL,&pass));
    VkFramebufferCreateInfo frameInfo={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&view,.width=W,.height=H,.layers=1};VkFramebuffer framebuffer;OK(vkCreateFramebuffer(device,&frameInfo,NULL,&framebuffer));
    VkPipelineLayoutCreateInfo layoutInfo={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VkPipelineLayout layout;OK(vkCreatePipelineLayout(device,&layoutInfo,NULL,&layout));
    VkShaderModule vert=shader(device,argv[1]),frag=shader(device,argv[2]);VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vert,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo multi={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineColorBlendAttachmentState color={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&color};VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamicStates};
    VkGraphicsPipelineCreateInfo pipelineInfo={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&multi,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=layout,.renderPass=pass,.basePipelineIndex=-1};VkPipeline pipeline;OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,NULL,&pipeline));
    vkDestroyShaderModule(device,vert,NULL);vkDestroyShaderModule(device,frag,NULL);
    VkBufferCreateInfo bufferInfo={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=W*H*4,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};VkBuffer buffer;OK(vkCreateBuffer(device,&bufferInfo,NULL,&buffer));VkMemoryRequirements bufferReq;vkGetBufferMemoryRequirements(device,buffer,&bufferReq);alloc.allocationSize=bufferReq.size;alloc.memoryTypeIndex=type(physical,bufferReq.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);VkDeviceMemory bufferMemory;OK(vkAllocateMemory(device,&alloc,NULL,&bufferMemory));OK(vkBindBufferMemory(device,buffer,bufferMemory,0));uint8_t *rgba;OK(vkMapMemory(device,bufferMemory,0,VK_WHOLE_SIZE,0,(void **)&rgba));
    VkCommandPoolCreateInfo poolInfo={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool pool;OK(vkCreateCommandPool(device,&poolInfo,NULL,&pool));VkCommandBufferAllocateInfo commandInfo={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;OK(vkAllocateCommandBuffers(device,&commandInfo,&command));VkFenceCreateInfo fenceInfo={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;OK(vkCreateFence(device,&fenceInfo,NULL,&fence));
    for(unsigned round=0;round<3;round++){
        if(round){OK(vkResetCommandBuffer(command,0));OK(vkResetFences(device,1,&fence));}
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(command,&begin));
        VkClearValue clear={.color={{8.0f/255,12.0f/255,20.0f/255,1}}};VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea={{0,0},{W,H}},.clearValueCount=1,.pClearValues=&clear};vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        VkViewport vp={0,0,W,H,0,1};vkCmdSetViewport(command,0,1,&vp);VkRect2D scissor={{round==1?W/2:0,0},{round==1?W/2:W,H}};vkCmdSetScissor(command,0,1,&scissor);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={W,H,1}};vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);OK(vkEndCommandBuffer(command));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};OK(vkQueueSubmit(queue,1,&submit,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL));VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=bufferMemory,.size=VK_WHOLE_SIZE};OK(vkInvalidateMappedMemoryRanges(device,1,&range));pixels(rgba,round);
    }
    FILE *file=fopen(argv[3],"wb");assert(file);assert(fprintf(file,"P6\n%d %d\n255\n",W,H)>0);for(unsigned i=0;i<W*H;i++)assert(fwrite(rgba+i*4,1,3,file)==3);assert(!fclose(file));
    OK(vkDeviceWaitIdle(device));vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkUnmapMemory(device,bufferMemory);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,bufferMemory,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyImageView(device,view,NULL);vkDestroyImage(device,image,NULL);vkFreeMemory(device,imageMemory,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);puts("vulkan-render-ok");return 0;
}

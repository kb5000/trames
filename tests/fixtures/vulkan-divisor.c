// SPDX-License-Identifier: GPL-3.0-or-later
// Identical ordinary Vulkan client on host/guest. Verify native instance input
// rates with independently calculated pixels and tightly sized vertex buffers.
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(call) do { VkResult status=(call); if(status!=VK_SUCCESS){fprintf(stderr,"%s: %d at %d\n",#call,status,__LINE__);abort();} } while(0)
#define W 64
#define H 8
#define BYTES (W*H*4)
static VkPhysicalDevice physical;
static VkDevice device;
static VkQueue queue;
static VkCommandPool pool;
static VkCommandBuffer cmd;
static VkFence fence;
struct Image { VkImage image; VkImageView view; VkDeviceMemory memory; VkFormat format; VkImageAspectFlags aspects; };
struct Buffer { VkBuffer buffer; VkDeviceMemory memory; uint8_t *data; };
static uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p;vkGetPhysicalDeviceMemoryProperties(physical,&p);
    for(unsigned i=0;i<p.memoryTypeCount;i++)if((bits&(1u<<i)) && (p.memoryTypes[i].propertyFlags&flags)==flags)return i;
    abort();
}
static VkImageView view(struct Image i,VkImageAspectFlags aspects) {
    VkImageViewCreateInfo v={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=i.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=i.format,.subresourceRange={aspects,0,1,0,1}};
    VkImageView out;OK(vkCreateImageView(device,&v,NULL,&out));return out;
}
static struct Image image(VkFormat format,unsigned width,unsigned height,unsigned mips,unsigned layers,VkImageUsageFlags usage,VkImageAspectFlags aspects,int with_view) {
    struct Image i={.format=format,.aspects=aspects};
    VkImageCreateInfo c={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={width,height,1},.mipLevels=mips,.arrayLayers=layers,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
    OK(vkCreateImage(device,&c,NULL,&i.image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,i.image,&req);
    VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,0)};
    OK(vkAllocateMemory(device,&a,NULL,&i.memory));OK(vkBindImageMemory(device,i.image,i.memory,0));if(with_view)i.view=view(i,aspects);return i;
}
static void free_image(struct Image i) {if(i.view)vkDestroyImageView(device,i.view,NULL);vkDestroyImage(device,i.image,NULL);vkFreeMemory(device,i.memory,NULL);}
static struct Buffer buffer(VkBufferUsageFlags usage,unsigned bytes) {
    struct Buffer b;VkBufferCreateInfo c={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=usage};OK(vkCreateBuffer(device,&c,NULL,&b.buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.buffer,&req);VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    OK(vkAllocateMemory(device,&a,NULL,&b.memory));OK(vkBindBufferMemory(device,b.buffer,b.memory,0));OK(vkMapMemory(device,b.memory,0,bytes,0,(void **)&b.data));return b;
}
static void free_buffer(struct Buffer b) {vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.buffer,NULL);vkFreeMemory(device,b.memory,NULL);}
static void begin(void) {OK(vkResetCommandPool(device,pool,0));VkCommandBufferBeginInfo b={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(cmd,&b));}
static void finish(void) {
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(vkEndCommandBuffer(cmd));OK(vkResetFences(device,1,&fence));
    VkSubmitInfo s={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};OK(vkQueueSubmit(queue,1,&s,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));
}
static void transition(struct Image i,VkImageLayout old,VkImageLayout next,unsigned mips,unsigned layers) {
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT,.dstAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT,.oldLayout=old,.newLayout=next,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=i.image,.subresourceRange={i.aspects,0,mips,0,layers}};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
}
static void download(struct Image i,struct Buffer b,VkImageAspectFlags aspect,unsigned offset) {
    VkBufferImageCopy c={.bufferOffset=offset,.imageSubresource={aspect,0,0,1},.imageExtent={W,H,1}};
    vkCmdCopyImageToBuffer(cmd,i.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,b.buffer,1,&c);
}
static VkShaderModule shader(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0&&n%4==0);rewind(f);uint32_t *code=malloc(n);assert(code&&fread(code,1,n,f)==(size_t)n);fclose(f);
    VkShaderModuleCreateInfo s={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=n,.pCode=code};VkShaderModule out;OK(vkCreateShaderModule(device,&s,NULL,&out));free(code);return out;
}
static VkPipeline pipeline(VkRenderPass pass,VkPipelineLayout layout,VkShaderModule vert,VkShaderModule frag,unsigned da,unsigned db,int bridge) {
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vert,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
    // Deliberately sparse/out-of-order bindings; both instance rates vary independently.
    VkVertexInputBindingDescription bindings[]={{0,8,VK_VERTEX_INPUT_RATE_VERTEX},{5,24,VK_VERTEX_INPUT_RATE_INSTANCE},{2,8,VK_VERTEX_INPUT_RATE_INSTANCE}};
    VkVertexInputAttributeDescription attrs[]={{0,0,VK_FORMAT_R32G32_SFLOAT,0},{1,5,VK_FORMAT_R32G32B32A32_SFLOAT,4},{2,2,VK_FORMAT_R32_SFLOAT,0}};
    VkVertexInputBindingDivisorDescriptionEXT divisors[]={{5,da},{2,db}};
    VkPipelineVertexInputDivisorStateCreateInfoEXT ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT,.vertexBindingDivisorCount=2,.pVertexBindingDivisors=divisors};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.pNext=&ds,.vertexBindingDescriptionCount=3,.pVertexBindingDescriptions=bindings,.vertexAttributeDescriptionCount=3,.pVertexAttributeDescriptions=attrs};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};VkViewport vp={0,0,W,H,0,1};VkRect2D sc={{0,0},{W,H}};VkPipelineViewportStateCreateInfo vs={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineColorBlendAttachmentState output={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&output};
    VkGraphicsPipelineCreateInfo c={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vs,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&blend,.layout=layout,.renderPass=pass};VkPipeline out;
    if(bridge){
        divisors[1].binding=5;assert(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&c,NULL,&out)==VK_ERROR_FEATURE_NOT_PRESENT);assert(out==VK_NULL_HANDLE);
        divisors[1].binding=0;assert(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&c,NULL,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
        divisors[1].binding=9;assert(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&c,NULL,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
        divisors[1].binding=2;
    }
    OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&c,NULL,&out));return out;
}
static unsigned last(unsigned divisor,unsigned count,unsigned first) {return divisor?first+(count-1)/divisor:0;}
static void render(VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT features,unsigned maximum,VkShaderModule vert,VkShaderModule frag,int bridge,int reject) {
    struct Image color=image(VK_FORMAT_R8G8B8A8_UNORM,W,H,1,1,17,1,1);struct Buffer output=buffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT,BYTES+16);
    VkAttachmentDescription attachment={.format=color.format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&sub};VkRenderPass pass;OK(vkCreateRenderPass(device,&rp,NULL,&pass));
    VkFramebufferCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&color.view,.width=W,.height=H,.layers=1};VkFramebuffer fb;OK(vkCreateFramebuffer(device,&fc,NULL,&fb));VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VkPipelineLayout layout;OK(vkCreatePipelineLayout(device,&pl,NULL,&layout));
    const float corners[7][2]={{0,0},{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};struct Buffer positions=buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,8+sizeof(corners));memset(positions.data,0xa5,8);memcpy(positions.data+8,corners,sizeof(corners));
    unsigned rates[]={1,2,3,maximum,0,1},cases=0,pixels=0;
    for(unsigned test=0;test<6;test++) {
        unsigned da=rates[test],first=test==5?2:0,count=test==5?4:8;
        unsigned db=first?1:da==2?3:features.vertexAttributeInstanceRateZeroDivisor?0:1;
        if(da>maximum || (da==0&&!features.vertexAttributeInstanceRateZeroDivisor))continue;
        if(reject){da=2;db=1;first=1;assert(maximum>=2);}
        unsigned na=last(da,count,first)+1,nb=last(db,count,first)+1;
        unsigned bytes_a=8+(na-1)*24+4+16,bytes_b=8+(nb-1)*8+4;
        struct Buffer a=buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,bytes_a),b=buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,bytes_b);memset(a.data,0xa5,bytes_a);memset(b.data,0xa5,bytes_b);
        for(unsigned i=0;i<na;i++){float values[4]={(i&1)?1:0,(i&2)?1:0,123,456};memcpy(a.data+8+i*24+4,values,16);}
        for(unsigned i=0;i<nb;i++){float tone=(i&1)?0:1;memcpy(b.data+8+i*8,&tone,4);}
        VkPipeline p=pipeline(pass,layout,vert,frag,da,db,bridge&&test==0);
        for(unsigned indexed=0;indexed<3;indexed++) {
            unsigned size=indexed==1?2:4;struct Buffer indices=buffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,8+6*size);memset(indices.data,0xa5,8+6*size);const uint32_t numbers[]={2,3,4,4,3,7};for(unsigned i=0;i<6;i++)memcpy(indices.data+8+i*size,numbers+i,size);
            memset(output.data,0xa5,BYTES+16);begin();VkClearValue clear={.color={{0,0,0,0}}};VkRenderPassBeginInfo pb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{W,H}},.clearValueCount=1,.pClearValues=&clear};vkCmdBeginRenderPass(cmd,&pb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,p);
            VkBuffer buffers[]={positions.buffer,a.buffer,b.buffer};VkDeviceSize offsets[]={8,8,8};vkCmdBindVertexBuffers(cmd,0,1,buffers,offsets);vkCmdBindVertexBuffers(cmd,5,1,buffers+1,offsets+1);vkCmdBindVertexBuffers(cmd,2,1,buffers+2,offsets+2);
            if(reject){puts("divisor-reject-first-instance-armed");fflush(stdout);}
            if(indexed){vkCmdBindIndexBuffer(cmd,indices.buffer,8,indexed==1?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(cmd,6,count,0,-1,first);}else vkCmdDraw(cmd,6,count,1,first);
            assert(!reject);vkCmdEndRenderPass(cmd);transition(color,2,6,1,1);download(color,output,1,0);finish();
            for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
                unsigned instance=x/8,active=instance>=first && instance<first+count,iax=da?first+(instance-first)/da:0,ibx=db?first+(instance-first)/db:0;
                uint8_t want[]={active&&(iax&1)?255:0,active&&(iax&2)?255:0,active&&!(ibx&1)?255:0,active?255:0};
                if(memcmp(output.data+(y*W+x)*4,want,4)){fprintf(stderr,"divisor pixel mismatch: rate=%u/%u first=%u indexed=%u pixel=%u,%u\n",da,db,first,indexed,x,y);abort();}pixels++;
            }
            for(unsigned i=BYTES;i<BYTES+16;i++)assert(output.data[i]==0xa5);
            free_buffer(indices);cases++;
        }
        vkDestroyPipeline(device,p,NULL);free_buffer(a);free_buffer(b);
    }
    printf("divisor-render-ok: %u draws / %u exact RGBA pixels, native divisor 0/1/2/3/max, independent bindings, tight buffers, firstInstance, direct/16-bit/32-bit indexed and negative baseVertex\n",cases,pixels);
    free_buffer(positions);free_buffer(output);free_image(color);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyFramebuffer(device,fb,NULL);vkDestroyRenderPass(device,pass,NULL);
}
int main(int argc,char **argv) {
    assert(argc==3||argc==4);int bridge=argc==4&&!strcmp(argv[3],"bridge-check"),reject=argc==4&&!strcmp(argv[3],"reject-first-instance");
    const char *ie=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.enabledExtensionCount=1,.ppEnabledExtensionNames=&ie};VkInstance instance;OK(vkCreateInstance(&ci,NULL,&instance));uint32_t count=1;OK(vkEnumeratePhysicalDevices(instance,&count,&physical));
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);fprintf(stderr,"divisor driver: %s\n",props.deviceName);
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT};VkPhysicalDeviceFeatures2 f={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&features};PFN_vkGetPhysicalDeviceFeatures2KHR get=(void *)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceFeatures2KHR");assert(get);get(physical,&f);assert(features.vertexAttributeInstanceRateDivisor);
    VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT limits={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT};VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&limits};PFN_vkGetPhysicalDeviceProperties2KHR get_props=(void *)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceProperties2KHR");assert(get_props);get_props(physical,&properties);assert(limits.maxVertexAttribDivisor>=3);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);VkQueueFamilyProperties *q=calloc(count,sizeof(*q));assert(q);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,q);unsigned family=0;while(family<count&&!(q[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<count);free(q);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};const char *ext=VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME;VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&features,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=1,.ppEnabledExtensionNames=&ext};
    if(bridge){VkDeviceCreateInfo missing=di;missing.enabledExtensionCount=0;VkDevice out=VK_NULL_HANDLE;assert(vkCreateDevice(physical,&missing,NULL,&out)==VK_ERROR_FEATURE_NOT_PRESENT);assert(out==VK_NULL_HANDLE);}
    OK(vkCreateDevice(physical,&di,NULL,&device));vkGetDeviceQueue(device,family,0,&queue);VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};OK(vkCreateCommandPool(device,&pc,NULL,&pool));VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.commandBufferCount=1};OK(vkAllocateCommandBuffers(device,&ca,&cmd));VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};OK(vkCreateFence(device,&fc,NULL,&fence));
    printf("divisor-features: max=%u zero=%u\n",limits.maxVertexAttribDivisor,features.vertexAttributeInstanceRateZeroDivisor);VkShaderModule vert=shader(argv[1]),frag=shader(argv[2]);render(features,limits.maxVertexAttribDivisor,vert,frag,bridge,reject);vkDestroyShaderModule(device,vert,NULL);vkDestroyShaderModule(device,frag,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyFence(device,fence,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);puts("vulkan-divisor-ok");return 0;
}

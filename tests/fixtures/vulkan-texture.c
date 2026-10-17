// SPDX-License-Identifier: GPL-3.0-or-later
// Standard native/guest application: indexed textured quads, two sampler paths,
// instanced vertex input and independent nearest/bilinear pixel equations.
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
static int static_blend;
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
    const double colors[4][3]={{192,32,64},{32,192,96},{64,96,224},{224,192,32}};
    unsigned checked=0,colored=0;
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){
        double px=2.0*(x+0.5)/W-1,py=2.0*(y+0.5)/H-1;
        double shift=round==2?0:0.125, u=(px-shift+0.75)/1.5,v=(py-shift+0.75)/1.5;
        if(fabs(u)<0.01 || fabs(u-1)<0.01 || fabs(v)<0.01 || fabs(v-1)<0.01)continue;
        int inside=u>0 && u<1 && v>0 && v<1;
        if(inside && round!=1 && (fabs(u-0.5)<0.01 || fabs(v-0.5)<0.01))continue;
        double expected[3]={8,12,20};
        if(inside){
            if(round!=1){for(unsigned c=0;c<3;c++)expected[c]=colors[(v>=0.5)*2+(u>=0.5)][c];}
            else {
                double tx=fmax(0,fmin(1,u*2-0.5)),ty=fmax(0,fmin(1,v*2-0.5));
                for(unsigned c=0;c<3;c++)expected[c]=((1-tx)*colors[0][c]+tx*colors[1][c])*(1-ty)+((1-tx)*colors[2][c]+tx*colors[3][c])*ty;
                expected[0]*=0.5;expected[1]*=0.25;
            }
            colored++;
        }
        if(inside && static_blend)for(unsigned c=0;c<3;c++)expected[c]=0.5*expected[c]+0.5*(double[]){8,12,20}[c];
        const uint8_t *p=rgba+(y*W+x)*4;
        for(unsigned c=0;c<3;c++)if(fabs(p[c]-expected[c])>2.0){fprintf(stderr,"texture pixel %u,%u channel %u got %u expected %.3f round %u\n",x,y,c,p[c],expected[c],round);abort();}
        assert(p[3]==(inside && static_blend?128:255));checked++;
    }
    assert(checked>W*H*9/10 && colored>W*H/3);
    printf("texture round %u: %s, vertex/instance input, uniforms and push constants correct\n",round+1,round==0?"uint16 indexed nearest":round==1?"uint32 indexed linear":"non-indexed nearest");
}
static VkBuffer host_buffer(VkPhysicalDevice physical,VkDevice d,VkDeviceSize size,VkBufferUsageFlags usage,const void *data,VkDeviceMemory *memory) {
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};VkBuffer buffer;OK(vkCreateBuffer(d,&info,NULL,&buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(d,buffer,&req);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type(physical,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)};
    OK(vkAllocateMemory(d,&alloc,NULL,memory));OK(vkBindBufferMemory(d,buffer,*memory,0));void *mapped;OK(vkMapMemory(d,*memory,0,VK_WHOLE_SIZE,0,&mapped));memcpy(mapped,data,(size_t)size);
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=*memory,.size=VK_WHOLE_SIZE};OK(vkFlushMappedMemoryRanges(d,1,&range));vkUnmapMemory(d,*memory);return buffer;
}
int main(int argc,char **argv) {
    assert(argc>=4 && argc<=8);int bridge=0,shifted=0,anisotropy=0;
    for(int i=4;i<argc;i++){if(!strcmp(argv[i],"bridge-check"))bridge=1;else if(!strcmp(argv[i],"base-offsets"))shifted=1;else if(!strcmp(argv[i],"anisotropy"))anisotropy=1;else if(!strcmp(argv[i],"static-blend"))static_blend=1;else abort();}
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VkInstance instance;OK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t n=1;VkPhysicalDevice physical;OK(vkEnumeratePhysicalDevices(instance,&n,&physical));assert(n==1);
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);fprintf(stderr,"Vulkan texture driver: %s\n",properties.deviceName);
    VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,&fp);assert(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    VkImageFormatProperties imageProperties;OK(vkGetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,0,&imageProperties));assert(imageProperties.maxExtent.width>=W && imageProperties.maxExtent.height>=H);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,NULL);VkQueueFamilyProperties *families=calloc(n,sizeof(*families));assert(families);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families);uint32_t family=0;while(family<n && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<n);free(families);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkPhysicalDeviceFeatures features={0};if(anisotropy){vkGetPhysicalDeviceFeatures(physical,&features);assert(features.samplerAnisotropy);memset(&features,0,sizeof(features));features.samplerAnisotropy=VK_TRUE;di.pEnabledFeatures=&features;}VkDevice device;OK(vkCreateDevice(physical,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkImageCreateInfo imageInfo={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={W,H,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};VkImage image;
    if(bridge){VkImageCreateInfo bad=imageInfo;bad.extent.width=0;assert(vkCreateImage(device,&bad,NULL,&image)!=VK_SUCCESS);bad=imageInfo;bad.format=VK_FORMAT_D32_SFLOAT_S8_UINT;assert(vkCreateImage(device,&bad,NULL,&image)==VK_ERROR_FORMAT_NOT_SUPPORTED);}
    OK(vkCreateImage(device,&imageInfo,NULL,&image));VkMemoryRequirements imageReq;vkGetImageMemoryRequirements(device,image,&imageReq);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=imageReq.size,.memoryTypeIndex=type(physical,imageReq.memoryTypeBits,0)};VkDeviceMemory imageMemory;OK(vkAllocateMemory(device,&alloc,NULL,&imageMemory));
    if(bridge)assert(vkBindImageMemory(device,image,imageMemory,imageReq.size)==VK_ERROR_UNKNOWN);
    OK(vkBindImageMemory(device,image,imageMemory,0));
    VkImageViewCreateInfo viewInfo={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};VkImageView view;OK(vkCreateImageView(device,&viewInfo,NULL,&view));
    VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};VkSubpassDependency dependency={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};VkRenderPassCreateInfo passInfo={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=1,.pDependencies=&dependency};VkRenderPass pass;OK(vkCreateRenderPass(device,&passInfo,NULL,&pass));
    VkFramebufferCreateInfo frameInfo={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&view,.width=W,.height=H,.layers=1};VkFramebuffer framebuffer;OK(vkCreateFramebuffer(device,&frameInfo,NULL,&framebuffer));
    VkDescriptorSetLayoutBinding descBindings[5]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=2,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},{.binding=2,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},{.binding=3,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},{.binding=4,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT}};
    VkDescriptorSetLayoutCreateInfo setInfo={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=5,.pBindings=descBindings};VkDescriptorSetLayout setLayout;OK(vkCreateDescriptorSetLayout(device,&setInfo,NULL,&setLayout));
    VkPushConstantRange push={VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,16};
    VkPipelineLayoutCreateInfo layoutInfo={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&setLayout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};VkPipelineLayout layout;OK(vkCreatePipelineLayout(device,&layoutInfo,NULL,&layout));
    VkShaderModule vert=shader(device,argv[1]),frag=shader(device,argv[2]);VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vert,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
    VkVertexInputBindingDescription vertexBindings[2]={{0,16,VK_VERTEX_INPUT_RATE_VERTEX},{1,8,VK_VERTEX_INPUT_RATE_INSTANCE}};
    VkVertexInputAttributeDescription attributes[3]={{0,0,VK_FORMAT_R32G32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,8},{2,1,VK_FORMAT_R32G32_SFLOAT,0}};
    VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=2,.pVertexBindingDescriptions=vertexBindings,.vertexAttributeDescriptionCount=3,.pVertexAttributeDescriptions=attributes};VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,.primitiveRestartEnable=VK_TRUE};VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo multi={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineColorBlendAttachmentState color={.colorWriteMask=15,.blendEnable=static_blend,.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,.colorBlendOp=VK_BLEND_OP_ADD,.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO,.alphaBlendOp=VK_BLEND_OP_ADD};VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&color};VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamicStates};
    VkGraphicsPipelineCreateInfo pipelineInfo={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&multi,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=layout,.renderPass=pass,.basePipelineIndex=-1};VkViewport staticViewport={0,0,W,H,0,1};VkRect2D staticScissor={{0,0},{W,H}};if(static_blend){viewport.pViewports=&staticViewport;viewport.pScissors=&staticScissor;pipelineInfo.pDynamicState=NULL;}VkPipeline pipeline;OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,NULL,&pipeline));
    vkDestroyShaderModule(device,vert,NULL);vkDestroyShaderModule(device,frag,NULL);
    VkBufferCreateInfo bufferInfo={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=W*H*4,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};VkBuffer buffer;OK(vkCreateBuffer(device,&bufferInfo,NULL,&buffer));VkMemoryRequirements bufferReq;vkGetBufferMemoryRequirements(device,buffer,&bufferReq);alloc.allocationSize=bufferReq.size;alloc.memoryTypeIndex=type(physical,bufferReq.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);VkDeviceMemory bufferMemory;OK(vkAllocateMemory(device,&alloc,NULL,&bufferMemory));OK(vkBindBufferMemory(device,buffer,bufferMemory,0));uint8_t *rgba;OK(vkMapMemory(device,bufferMemory,0,VK_WHOLE_SIZE,0,(void **)&rgba));
    VkCommandPoolCreateInfo poolInfo={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool pool;OK(vkCreateCommandPool(device,&poolInfo,NULL,&pool));VkCommandBufferAllocateInfo commandInfo={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;OK(vkAllocateCommandBuffers(device,&commandInfo,&command));VkFenceCreateInfo fenceInfo={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;OK(vkCreateFence(device,&fenceInfo,NULL,&fence));
    // Each vertex buffer has a nonzero binding offset and a dummy vertex.
    float vertexData[24]={99,99,99,99, 99,99,99,99, -0.75f,-0.75f,0,0, 0.75f,-0.75f,1,0, -0.75f,0.75f,0,1, 0.75f,0.75f,1,1};
    float instanceData[6]={99,99,99,99,0.125f,0.125f};
    uint16_t indices16[5]={0xffff,1,2,3,4};uint32_t indices32[5]={0xffffffff,1,2,3,4};
    if(shifted)for(unsigned i=1;i<5;i++){indices16[i]--;indices32[i]++;}
    uint8_t upload[48]={0};const uint8_t texels[16]={192,32,64,255,32,192,96,255,64,96,224,255,224,192,32,255};memcpy(upload+16,texels,8);memcpy(upload+32,texels+8,8);
    const float identity[4]={1,1,1,static_blend?0.5f:1};
    VkDeviceMemory extraMemory[6];VkBuffer extra[6];
    extra[0]=host_buffer(physical,device,sizeof(vertexData),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertexData,&extraMemory[0]);
    extra[1]=host_buffer(physical,device,sizeof(instanceData),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,instanceData,&extraMemory[1]);
    extra[2]=host_buffer(physical,device,sizeof(indices16),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,indices16,&extraMemory[2]);
    extra[3]=host_buffer(physical,device,sizeof(indices32),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,indices32,&extraMemory[3]);
    extra[4]=host_buffer(physical,device,sizeof(upload),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,upload,&extraMemory[4]);
    extra[5]=host_buffer(physical,device,sizeof(identity),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,identity,&extraMemory[5]);
    VkImageCreateInfo textureInfo=imageInfo;textureInfo.extent=(VkExtent3D){2,2,1};textureInfo.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage texture;OK(vkCreateImage(device,&textureInfo,NULL,&texture));VkMemoryRequirements textureReq;vkGetImageMemoryRequirements(device,texture,&textureReq);
    alloc.allocationSize=textureReq.size;alloc.memoryTypeIndex=type(physical,textureReq.memoryTypeBits,0);VkDeviceMemory textureMemory;OK(vkAllocateMemory(device,&alloc,NULL,&textureMemory));OK(vkBindImageMemory(device,texture,textureMemory,0));
    viewInfo.image=texture;VkImageView textureView;OK(vkCreateImageView(device,&viewInfo,NULL,&textureView));
    VkSampler samplers[2];for(unsigned i=0;i<2;i++){VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.anisotropyEnable=anisotropy && i,.maxAnisotropy=16};OK(vkCreateSampler(device,&sampler,NULL,&samplers[i]));}
    VkDescriptorPoolSize sizes[4]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2}};
    VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=4,.pPoolSizes=sizes};VkDescriptorPool descriptorPool;OK(vkCreateDescriptorPool(device,&dp,NULL,&descriptorPool));
    VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptorPool,.descriptorSetCount=1,.pSetLayouts=&setLayout};VkDescriptorSet set;OK(vkAllocateDescriptorSets(device,&da,&set));
    for(unsigned round=0;round<3;round++){
        if(round){OK(vkResetCommandBuffer(command,0));OK(vkResetFences(device,1,&fence));}
        VkDescriptorImageInfo sampled={.imageView=textureView,.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},samplerOnly={.sampler=samplers[round==1]};
        VkDescriptorImageInfo combined[2]={{samplers[round==1],textureView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{samplers[round==1],textureView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkDescriptorBufferInfo uniform={extra[5],0,16};VkWriteDescriptorSet writes[5];memset(writes,0,sizeof(writes));
        for(unsigned i=0;i<5;i++){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorType=descBindings[i].descriptorType;writes[i].descriptorCount=descBindings[i].descriptorCount;}
        writes[0].pImageInfo=combined;writes[1].pImageInfo=&sampled;writes[2].pImageInfo=&samplerOnly;writes[3].pBufferInfo=&uniform;writes[4].pBufferInfo=&uniform;vkUpdateDescriptorSets(device,5,writes,0,NULL);
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(command,&begin));
        if(!round){
            VkImageMemoryBarrier toUpload={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=texture,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&toUpload);
            VkBufferImageCopy uploadCopy={.bufferOffset=16,.bufferRowLength=4,.bufferImageHeight=3,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={2,2,1}};vkCmdCopyBufferToImage(command,extra[4],texture,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&uploadCopy);
            toUpload.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;toUpload.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;toUpload.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;toUpload.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&toUpload);
        }
        VkMemoryBarrier vertexReady={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,1,&vertexReady,0,NULL,0,NULL);
        VkClearValue clear={.color={{8.0f/255,12.0f/255,20.0f/255,1}}};VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea={{0,0},{W,H}},.clearValueCount=1,.pClearValues=&clear};vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        VkViewport vp={0,0,W,H,0,1};if(!static_blend)vkCmdSetViewport(command,0,1,&vp);VkRect2D scissor={{0,0},{W,H}};if(!static_blend)vkCmdSetScissor(command,0,1,&scissor);
        VkDeviceSize offsets[2]={16,shifted?8:16};vkCmdBindVertexBuffers(command,0,2,extra,offsets);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
        float constants[4]={round==2?-0.125f:0,round==2?-0.125f:0,round==1?0.5f:1,round==1?0.25f:1};vkCmdPushConstants(command,layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(constants),constants);
        if(round<2){vkCmdBindIndexBuffer(command,extra[round+2],0,round?VK_INDEX_TYPE_UINT32:VK_INDEX_TYPE_UINT16);vkCmdDrawIndexed(command,4,1,1,shifted?(round?-1:1):0,shifted?1:0);}else vkCmdDraw(command,4,1,1,shifted?1:0);vkCmdEndRenderPass(command);
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={W,H,1}};vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);OK(vkEndCommandBuffer(command));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};OK(vkQueueSubmit(queue,1,&submit,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL));VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=bufferMemory,.size=VK_WHOLE_SIZE};OK(vkInvalidateMappedMemoryRanges(device,1,&range));pixels(rgba,round);
    }
    if(anisotropy){
        OK(vkResetCommandBuffer(command,0));OK(vkResetFences(device,1,&fence));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(command,&begin));
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkClearColorValue color={.float32={16.0f/255,80.0f/255,192.0f/255,1}};
        vkCmdClearColorImage(command,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&barrier.subresourceRange);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={W,H,1}};vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
        OK(vkEndCommandBuffer(command));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};OK(vkQueueSubmit(queue,1,&submit,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL));
        VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=bufferMemory,.size=VK_WHOLE_SIZE};OK(vkInvalidateMappedMemoryRanges(device,1,&range));
        for(unsigned i=0;i<W*H;i++)assert(rgba[i*4]==16 && rgba[i*4+1]==80 && rgba[i*4+2]==192 && rgba[i*4+3]==255);
        puts("native anisotropic sampler and transfer color clear: correct");
    }
    if(static_blend)puts("static viewport/scissor and source-alpha blending: correct");
    FILE *file=fopen(argv[3],"wb");assert(file);assert(fprintf(file,"P6\n%d %d\n255\n",W,H)>0);for(unsigned i=0;i<W*H;i++)assert(fwrite(rgba+i*4,1,3,file)==3);assert(!fclose(file));
    OK(vkDeviceWaitIdle(device));vkDestroyDescriptorPool(device,descriptorPool,NULL);vkDestroyDescriptorSetLayout(device,setLayout,NULL);for(unsigned i=0;i<2;i++)vkDestroySampler(device,samplers[i],NULL);vkDestroyImageView(device,textureView,NULL);vkDestroyImage(device,texture,NULL);vkFreeMemory(device,textureMemory,NULL);for(unsigned i=0;i<6;i++){vkDestroyBuffer(device,extra[i],NULL);vkFreeMemory(device,extraMemory[i],NULL);}vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkUnmapMemory(device,bufferMemory);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,bufferMemory,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyImageView(device,view,NULL);vkDestroyImage(device,image,NULL);vkFreeMemory(device,imageMemory,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);if(shifted)puts("nonzero base vertex/instance: correct");puts("vulkan-texture-ok");return 0;
}

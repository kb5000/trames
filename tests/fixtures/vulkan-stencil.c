// SPDX-License-Identifier: GPL-3.0-or-later
// Same ordinary Vulkan client on host and guest. Expected pixels and stencil
// values come from scalar Vulkan equations, never a second renderer or GL override.
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(call) do { VkResult status=(call); if(status!=VK_SUCCESS){fprintf(stderr,"%s: %d at %d\n",#call,status,__LINE__);abort();} } while(0)
#define W 64
#define H 48
#define BYTES (256*1024)
static VkPhysicalDevice physical;
static VkDevice device;
static VkQueue queue;
static VkCommandPool pool;
static VkCommandBuffer cmd;
static VkFence fence;
static PFN_vkCreateRenderPass2KHR create2;
static PFN_vkCmdBeginRenderPass2KHR begin2;
static PFN_vkCmdEndRenderPass2KHR end2;
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
static struct Buffer buffer(VkBufferUsageFlags usage) {
    struct Buffer b;VkBufferCreateInfo c={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=BYTES,.usage=usage};OK(vkCreateBuffer(device,&c,NULL,&b.buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.buffer,&req);VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    OK(vkAllocateMemory(device,&a,NULL,&b.memory));OK(vkBindBufferMemory(device,b.buffer,b.memory,0));OK(vkMapMemory(device,b.memory,0,BYTES,0,(void **)&b.data));return b;
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
static void transfers(struct Buffer input,struct Buffer output) {
    unsigned verified=0;
    for(unsigned f=124;f<=130;f++) {
        VkImageFormatProperties p;VkResult supported=vkGetPhysicalDeviceImageFormatProperties(physical,f,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&p);
        if(supported==VK_ERROR_FORMAT_NOT_SUPPORTED){printf("stencil-format-unavailable: %u\n",f);continue;}OK(supported);
        unsigned aspects=f<=126?2:f==127?4:6;
        struct Image src=image(f,8,4,2,2,3,aspects,0),dst=image(f,8,4,2,2,3,aspects,0);
        memset(input.data,0xa5,BYTES);memset(output.data,0xa5,BYTES);begin();transition(src,0,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,2,2);transition(dst,0,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,2,2);
        VkBufferImageCopy regions[2];VkImageCopy copies[2];unsigned count=0;
        for(unsigned aspect=2;aspect<=4;aspect*=2)if(aspects&aspect) {
            unsigned bytes=aspect==4?1:(f==124||f==128)?2:4,offset=aspect==2?0:1024;
            for(unsigned layer=0;layer<2;layer++)for(unsigned y=0;y<2;y++)for(unsigned x=0;x<4;x++) {
                unsigned at=offset+((layer*6+y)*12+x)*bytes,value=0x2340+x+y*4+layer*8;
                if(aspect==4)input.data[at]=(uint8_t)(value*3);
                else if(f==126||f==130){float v=0.25f+(x+y*4+layer*8)/128.f;memcpy(input.data+at,&v,4);}
                else memcpy(input.data+at,&value,bytes);
            }
            regions[count]=(VkBufferImageCopy){.bufferOffset=offset,.bufferRowLength=12,.bufferImageHeight=6,.imageSubresource={aspect,1,0,2},.imageExtent={4,2,1}};
            copies[count]=(VkImageCopy){.srcSubresource={aspect,1,0,2},.dstSubresource={aspect,1,0,2},.extent={4,2,1}};count++;
        }
        vkCmdCopyBufferToImage(cmd,input.buffer,src.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,count,regions);transition(src,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,2,2);
        vkCmdCopyImage(cmd,src.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dst.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,count,copies);transition(dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,2,2);
        for(unsigned i=0;i<count;i++)regions[i].bufferOffset+=2048;
        vkCmdCopyImageToBuffer(cmd,dst.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.buffer,count,regions);finish();
        uint8_t expected[4096];memset(expected,0xa5,sizeof(expected));
        for(unsigned i=0;i<count;i++) {
            unsigned aspect=regions[i].imageSubresource.aspectMask,bytes=aspect==4?1:(f==124||f==128)?2:4,offset=aspect==2?0:1024;
            for(unsigned layer=0;layer<2;layer++)for(unsigned y=0;y<2;y++)for(unsigned x=0;x<4;x++) {
                unsigned at=offset+((layer*6+y)*12+x)*bytes;memcpy(expected+2048+at,input.data+at,bytes);
                // X8_D24 has eight undefined high bits in buffer transfers.
                if((f==125||f==129)&&aspect==2)expected[2048+at+3]=output.data[2048+at+3];
            }
        }
        assert(!memcmp(expected,output.data,sizeof(expected)));free_image(src);free_image(dst);verified++;
    }
    assert(verified>=2);printf("stencil-transfer-ok: %u supported formats, separate depth/stencil footprints, mip/layer copies and untouched row/tail padding\n",verified);
}
static VkShaderModule shader(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0&&n%4==0);rewind(f);uint32_t *code=malloc(n);assert(code&&fread(code,1,n,f)==(size_t)n);fclose(f);
    VkShaderModuleCreateInfo s={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=n,.pCode=code};VkShaderModule out;OK(vkCreateShaderModule(device,&s,NULL,&out));free(code);return out;
}
static VkRenderPass pass(int two,int depth) {
    VkAttachmentDescription a[2]={{.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {.format=VK_FORMAT_D32_SFLOAT_S8_UINT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE,.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},z={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if(!two){VkSubpassDescription s={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color,.pDepthStencilAttachment=depth?&z:NULL};VkRenderPassCreateInfo p={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=depth?2:1,.pAttachments=a,.subpassCount=1,.pSubpasses=&s};VkRenderPass out;OK(vkCreateRenderPass(device,&p,NULL,&out));return out;}
    VkAttachmentDescription2 a2[2];for(unsigned i=0;i<2;i++)a2[i]=(VkAttachmentDescription2){.sType=VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,.format=a[i].format,.samples=a[i].samples,.loadOp=a[i].loadOp,.storeOp=a[i].storeOp,.stencilLoadOp=a[i].stencilLoadOp,.stencilStoreOp=a[i].stencilStoreOp,.initialLayout=a[i].initialLayout,.finalLayout=a[i].finalLayout};
    VkAttachmentReference2 c2={.sType=VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,.attachment=0,.layout=color.layout,.aspectMask=1},z2={.sType=VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,.attachment=1,.layout=z.layout,.aspectMask=6};VkSubpassDescription2 s={.sType=VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&c2,.pDepthStencilAttachment=&z2};
    VkRenderPassCreateInfo2 p={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,.attachmentCount=2,.pAttachments=a2,.subpassCount=1,.pSubpasses=&s};VkRenderPass out;OK(create2(device,&p,NULL,&out));return out;
}
static VkFramebuffer framebuffer(VkRenderPass p,struct Image color,struct Image depth,int imageless,int with_depth) {
    VkImageView views[]={color.view,depth.view};VkFramebufferCreateInfo f={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=p,.attachmentCount=with_depth?2:1,.pAttachments=views,.width=W,.height=H,.layers=1};
    VkFormat formats[]={color.format,depth.format};VkFramebufferAttachmentImageInfo images[2];for(unsigned i=0;i<2;i++)images[i]=(VkFramebufferAttachmentImageInfo){.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,.usage=i?39:17,.width=W,.height=H,.layerCount=1,.viewFormatCount=1,.pViewFormats=formats+i};
    VkFramebufferAttachmentsCreateInfo a={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO,.attachmentImageInfoCount=2,.pAttachmentImageInfos=images};if(imageless){f.flags=VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;f.pNext=&a;f.pAttachments=NULL;}
    VkFramebuffer out;OK(vkCreateFramebuffer(device,&f,NULL,&out));return out;
}
static VkPipeline pipeline(VkRenderPass pass,VkPipelineLayout layout,VkShaderModule vert,VkShaderModule frag,int group,unsigned operation) {
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vert,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vs={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkStencilOpState front={.failOp=group==1?operation:VK_STENCIL_OP_INVERT,.passOp=(group==0||group==4)?operation:VK_STENCIL_OP_REPLACE,.depthFailOp=group==2?operation:VK_STENCIL_OP_ZERO,.compareOp=group==1?VK_COMPARE_OP_NEVER:group==3?operation:VK_COMPARE_OP_ALWAYS,.compareMask=63,.writeMask=243,.reference=53},back=front;
    back.compareMask=15;back.writeMask=95;back.reference=98;
    if(group==0||group==4)back.passOp=(operation+1)%8;
    VkPipelineDepthStencilStateCreateInfo z={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=group>=0,.depthWriteEnable=group>=0,.depthCompareOp=group==2?VK_COMPARE_OP_NEVER:VK_COMPARE_OP_LESS,.stencilTestEnable=group>=0,.front=front,.back=back};
    VkPipelineColorBlendAttachmentState attachment={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_REFERENCE};VkPipelineDynamicStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=group>=0&&group!=4?5:2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo c={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vs,.pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&z,.pColorBlendState=&bs,.pDynamicState=&ds,.layout=layout,.renderPass=pass};VkPipeline out;OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&c,NULL,&out));return out;
}
static int compare(unsigned op,unsigned ref,unsigned value) {switch(op){case 0:return 0;case 1:return ref<value;case 2:return ref==value;case 3:return ref<=value;case 4:return ref>value;case 5:return ref!=value;case 6:return ref>=value;default:return 1;}}
static unsigned apply(unsigned op,unsigned value,unsigned ref) {switch(op){case 0:return value;case 1:return 0;case 2:return ref;case 3:return value==255?255:value+1;case 4:return value?value-1:0;case 5:return value^255;case 6:return (value+1)&255;default:return (value-1)&255;}}
static unsigned expected(unsigned seed,unsigned row,unsigned col,int *passed) {
    int back=row==1||row==3||row==5;unsigned mask=back?15:63,write=back?95:243,ref=back?98:53;
    int stencil=row==2?0:row>=4?compare(col,ref&mask,seed&mask):1;*passed=stencil&&row!=3;
    unsigned op=!stencil?(row==2?col:5):row==3?col:row<=1?(back?(col+1)%8:col):2;
    return (seed&~write)|(apply(op,seed,ref)&write);
}
static void render(struct Buffer output,VkShaderModule vert,VkShaderModule frag,VkShaderModule sample) {
    struct Image color=image(37,W,H,1,1,17,1,1),depth=image(130,W,H,1,1,39,6,1);
    VkPushConstantRange range={VK_SHADER_STAGE_VERTEX_BIT,0,4};VkPipelineLayoutCreateInfo l={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.pushConstantRangeCount=1,.pPushConstantRanges=&range};VkPipelineLayout layout;OK(vkCreatePipelineLayout(device,&l,NULL,&layout));
    VkRenderPass passes[]={pass(0,1),pass(1,1)};VkFramebuffer fbs[]={framebuffer(passes[0],color,depth,0,1),framebuffer(passes[1],color,depth,1,1)};
    VkPipeline pipelines[4][8];for(unsigned g=0;g<4;g++)for(unsigned op=0;op<8;op++)pipelines[g][op]=pipeline(passes[0],layout,vert,frag,g,op);
    VkPipeline static_stencil=pipeline(passes[0],layout,vert,frag,4,2);
    for(unsigned round=0;round<6;round++) {
        unsigned seed=(unsigned[]){0,160,255}[round%3],two=round/3;memset(output.data,0xa5,BYTES);begin();
        VkClearValue clears[2]={{{{0,0,0,1}}},{.depthStencil={.depth=.75f,.stencil=seed}}};VkImageView views[]={color.view,depth.view};VkRenderPassAttachmentBeginInfo attachments={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,.attachmentCount=2,.pAttachments=views};VkRenderPassBeginInfo pb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.pNext=two?&attachments:NULL,.renderPass=passes[two],.framebuffer=fbs[two],.renderArea={{0,0},{W,H}},.clearValueCount=2,.pClearValues=clears};VkSubpassBeginInfo sb={.sType=VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO};
        if(two)begin2(cmd,&pb,&sb);else vkCmdBeginRenderPass(cmd,&pb,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport={0,0,W,H,0,1};vkCmdSetViewport(cmd,0,1,&viewport);float z=.25f;vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,4,&z);
        vkCmdSetStencilCompareMask(cmd,VK_STENCIL_FACE_FRONT_BIT,63);vkCmdSetStencilCompareMask(cmd,VK_STENCIL_FACE_BACK_BIT,15);vkCmdSetStencilWriteMask(cmd,VK_STENCIL_FACE_FRONT_BIT,243);vkCmdSetStencilWriteMask(cmd,VK_STENCIL_FACE_BACK_BIT,95);vkCmdSetStencilReference(cmd,VK_STENCIL_FACE_FRONT_BIT,53);vkCmdSetStencilReference(cmd,VK_STENCIL_FACE_BACK_BIT,98);
        for(unsigned row=0;row<6;row++)for(unsigned col=0;col<8;col++) {
            unsigned group=row<=1?0:row==2?1:row==3?2:3;vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,(row<=1&&col==2)?static_stencil:pipelines[group][col]);VkRect2D scissor={{col*8,row*8},{8,8}};vkCmdSetScissor(cmd,0,1,&scissor);vkCmdDraw(cmd,3,1,(row==1||row==3||row==5)?3:0,0);
        }
        if(two){VkSubpassEndInfo se={.sType=VK_STRUCTURE_TYPE_SUBPASS_END_INFO};end2(cmd,&se);}else vkCmdEndRenderPass(cmd);
        transition(color,2,6,1,1);transition(depth,3,6,1,1);download(color,output,1,0);download(depth,output,2,16384);download(depth,output,4,32768);finish();
        for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
            int passed;unsigned want=expected(seed,y/8,x/8,&passed),at=y*W+x;float got;memcpy(&got,output.data+16384+at*4,4);
            if(output.data[32768+at]!=want||got!=(passed?.25f:.75f)||output.data[at*4]!=(passed?255:0)){fprintf(stderr,"stencil mismatch round=%u pixel=%u,%u stencil=%u/%u depth=%f pass=%d color=%u\n",round,x,y,output.data[32768+at],want,got,passed,output.data[at*4]);abort();}
            assert(output.data[at*4+1]==0 && output.data[at*4+2]==(passed?255:0) && output.data[at*4+3]==255);
        }
    }
    puts("stencil-render-ok: 18432 exact pixels, all eight operations/compares, front/back, fail/depth-fail, static/dynamic masks/references, clamp/wrap edges, legacy/bound and renderpass2/imageless");
    // Each partial clear must preserve the other aspect, then both views are
    // sampled by a real fragment shader (including native depth comparison).
    begin();transition(depth,6,7,1,1);VkClearDepthStencilValue clear={.depth=.75f,.stencil=90};VkImageSubresourceRange r={2,0,1,0,1};vkCmdClearDepthStencilImage(cmd,depth.image,7,&clear,1,&r);transition(depth,7,6,1,1);download(depth,output,4,65536);transition(depth,6,7,1,1);r.aspectMask=4;vkCmdClearDepthStencilImage(cmd,depth.image,7,&clear,1,&r);transition(depth,7,6,1,1);download(depth,output,2,81920);download(depth,output,4,98304);transition(depth,6,4,1,1);
    VkDescriptorSetLayoutBinding bindings[2];for(unsigned i=0;i<2;i++)bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};VkDescriptorSetLayoutCreateInfo sl={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};VkDescriptorSetLayout set_layout;OK(vkCreateDescriptorSetLayout(device,&sl,NULL,&set_layout));
    l.setLayoutCount=1;l.pSetLayouts=&set_layout;VkPipelineLayout sample_layout;OK(vkCreatePipelineLayout(device,&l,NULL,&sample_layout));VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2};VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&size};VkDescriptorPool descriptors;OK(vkCreateDescriptorPool(device,&dp,NULL,&descriptors));VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptors,.descriptorSetCount=1,.pSetLayouts=&set_layout};VkDescriptorSet set;OK(vkAllocateDescriptorSets(device,&sa,&set));
    VkImageView depth_view=view(depth,2),stencil_view=view(depth,4);VkSampler samplers[2];for(unsigned i=0;i<2;i++){VkSamplerCreateInfo s={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.compareEnable=i==0,.compareOp=VK_COMPARE_OP_LESS};OK(vkCreateSampler(device,&s,NULL,&samplers[i]));}
    VkDescriptorImageInfo infos[]={{samplers[0],depth_view,4},{samplers[1],stencil_view,4}};VkWriteDescriptorSet writes[2];for(unsigned i=0;i<2;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=infos+i};vkUpdateDescriptorSets(device,2,writes,0,NULL);
    VkRenderPass sp=pass(0,0);VkFramebuffer sf=framebuffer(sp,color,depth,0,0);VkPipeline sample_pipeline=pipeline(sp,sample_layout,vert,sample,-1,0);VkClearValue black={.color={{0,0,0,1}}};VkRenderPassBeginInfo pb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=sp,.framebuffer=sf,.renderArea={{0,0},{W,H}},.clearValueCount=1,.pClearValues=&black};vkCmdBeginRenderPass(cmd,&pb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,0,sample_pipeline);VkViewport vp={0,0,W,H,0,1};VkRect2D sc={{0,0},{W,H}};vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&sc);vkCmdBindDescriptorSets(cmd,0,sample_layout,0,1,&set,0,NULL);float z=.25f;vkCmdPushConstants(cmd,sample_layout,VK_SHADER_STAGE_VERTEX_BIT,0,4,&z);vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderPass(cmd);transition(color,2,6,1,1);download(color,output,1,0);finish();
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){unsigned at=y*W+x;int unused;assert(output.data[65536+at]==expected(255,y/8,x/8,&unused));float value;memcpy(&value,output.data+81920+at*4,4);assert(value==.75f && output.data[98304+at]==90);assert(output.data[at*4]==255 && output.data[at*4+1]==90 && output.data[at*4+2]==0 && output.data[at*4+3]==255);}
    puts("stencil-sample-ok: independent aspect clears preserve the other component; native shadow compare and unsigned stencil texture readback, 3072 exact RGBA pixels");
    vkDestroyPipeline(device,sample_pipeline,NULL);vkDestroyFramebuffer(device,sf,NULL);vkDestroyRenderPass(device,sp,NULL);vkDestroyDescriptorPool(device,descriptors,NULL);for(unsigned i=0;i<2;i++)vkDestroySampler(device,samplers[i],NULL);vkDestroyImageView(device,depth_view,NULL);vkDestroyImageView(device,stencil_view,NULL);vkDestroyPipelineLayout(device,sample_layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);
    for(unsigned g=0;g<4;g++)for(unsigned op=0;op<8;op++)vkDestroyPipeline(device,pipelines[g][op],NULL);
    for(unsigned i=0;i<2;i++){vkDestroyFramebuffer(device,fbs[i],NULL);vkDestroyRenderPass(device,passes[i],NULL);}
    vkDestroyPipeline(device,static_stencil,NULL);vkDestroyPipelineLayout(device,layout,NULL);free_image(color);free_image(depth);
}
int main(int argc,char **argv) {
    assert(argc==4);const char *ie=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.enabledExtensionCount=1,.ppEnabledExtensionNames=&ie};VkInstance instance;OK(vkCreateInstance(&ci,NULL,&instance));uint32_t count=1;OK(vkEnumeratePhysicalDevices(instance,&count,&physical));
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);fprintf(stderr,"stencil driver: %s\n",properties.deviceName);
    VkPhysicalDeviceImagelessFramebufferFeatures imageless={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES};VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&imageless};PFN_vkGetPhysicalDeviceFeatures2KHR get=(void *)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceFeatures2KHR");assert(get);get(physical,&features);assert(imageless.imagelessFramebuffer);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);VkQueueFamilyProperties *q=calloc(count,sizeof(*q));assert(q);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,q);unsigned family=0;while(family<count&&!(q[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<count);free(q);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};const char *extensions[]={VK_KHR_MAINTENANCE_2_EXTENSION_NAME,VK_KHR_MULTIVIEW_EXTENSION_NAME,VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&imageless,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=5,.ppEnabledExtensionNames=extensions};OK(vkCreateDevice(physical,&di,NULL,&device));vkGetDeviceQueue(device,family,0,&queue);
    create2=(void *)vkGetDeviceProcAddr(device,"vkCreateRenderPass2KHR");begin2=(void *)vkGetDeviceProcAddr(device,"vkCmdBeginRenderPass2KHR");end2=(void *)vkGetDeviceProcAddr(device,"vkCmdEndRenderPass2KHR");assert(create2&&begin2&&end2);
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};OK(vkCreateCommandPool(device,&pc,NULL,&pool));VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.commandBufferCount=1};OK(vkAllocateCommandBuffers(device,&ca,&cmd));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};OK(vkCreateFence(device,&fi,NULL,&fence));
    struct Buffer input=buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT),output=buffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT);transfers(input,output);VkShaderModule vert=shader(argv[1]),frag=shader(argv[2]),sample=shader(argv[3]);render(output,vert,frag,sample);vkDestroyShaderModule(device,vert,NULL);vkDestroyShaderModule(device,frag,NULL);vkDestroyShaderModule(device,sample,NULL);
    vkDestroyCommandPool(device,pool,NULL);vkDestroyFence(device,fence,NULL);free_buffer(input);free_buffer(output);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);puts("vulkan-stencil-ok");return 0;
}

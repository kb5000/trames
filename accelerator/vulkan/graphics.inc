// SPDX-License-Identifier: GPL-3.0-or-later
// Core graphics frontend: up to eight color outputs, vertex/index input, sampled textures, dynamic
// static/dynamic viewport/scissor, depth and native blend/sampler state.
// Wire op 58..76, 92..97 and 114.
// Decode selected pNext records locally. Only their copied bodies enter wire calls.
static VkResult render_graph_legacy(VkDevice,const VkRenderPassCreateInfo *,VkRenderPass *);
static VkResult render_graph_modern(VkDevice,const VkRenderPassCreateInfo2 *,VkRenderPass *);
static int image_formats(const void *chain,const VkImageFormatListCreateInfo **list,const VkImageStencilUsageCreateInfo **stencil) {
    const VkBaseInStructure *next=chain;*list=NULL;*stencil=NULL;unsigned n=0;
    while(next && n++<16){
        if(next->sType==VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO && !*list){*list=(const void *)next;if((*list)->viewFormatCount>16)return 0;}
        else if(next->sType==VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO && !*stencil)*stencil=(const void *)next;
        else if(next->sType==VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO || next->sType==VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT ||
                next->sType==VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT || next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO ||
                next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT){}
        else return 0;
        next=next->pNext;
    }
    return !next;
}
static VkImageAspectFlags format_aspects(VkFormat format) {
    if(format>=VK_FORMAT_D16_UNORM && format<=VK_FORMAT_D32_SFLOAT)return VK_IMAGE_ASPECT_DEPTH_BIT;
    if(format==VK_FORMAT_S8_UINT)return VK_IMAGE_ASPECT_STENCIL_BIT;
    if(format>=VK_FORMAT_D16_UNORM_S8_UINT && format<=VK_FORMAT_D32_SFLOAT_S8_UINT)return VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice d,const VkImageCreateInfo *info,const VkAllocationCallbacks *allocator,VkImage *out) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;ALLOCATOR(allocator);
    const VkImageFormatListCreateInfo *list;const VkImageStencilUsageCreateInfo *stencil;if(!image_formats(info->pNext,&list,&stencil))return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkExternalMemoryImageCreateInfo *external=NULL;const VkImageDrmFormatModifierExplicitCreateInfoEXT *modifier=NULL;
    for(const VkBaseInStructure *n=info->pNext;n;n=n->pNext) {
        if(n->sType==VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO)external=(const void*)n;
        if(n->sType==VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT)modifier=(const void*)n;
        if(n->sType==VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT)return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    if(external && (external->handleTypes!=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT || !modifier ||
        modifier->drmFormatModifier || modifier->drmFormatModifierPlaneCount!=1))return VK_ERROR_FORMAT_NOT_SUPPORTED;
    uint64_t extra[8]={list?PTR(list->pViewFormats):0,list?list->viewFormatCount:0,stencil?stencil->stencilUsage:0,
        external?external->handleTypes:0,modifier?modifier->drmFormatModifier:0,modifier?modifier->pPlaneLayouts[0].rowPitch:0,modifier?modifier->pPlaneLayouts[0].offset:0,modifier?modifier->drmFormatModifierPlaneCount:0};
    if((info->flags&~(VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT|VK_IMAGE_CREATE_EXTENDED_USAGE_BIT|VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT|VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT)) || info->imageType>VK_IMAGE_TYPE_3D || !info->extent.depth || !info->mipLevels || !info->arrayLayers || !info->samples || info->samples>VK_SAMPLE_COUNT_64_BIT || (info->samples&(info->samples-1)) || (info->tiling>VK_IMAGE_TILING_LINEAR && !(external && info->tiling==VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)) || info->sharingMode!=VK_SHARING_MODE_EXCLUSIVE || (info->initialLayout!=VK_IMAGE_LAYOUT_UNDEFINED && info->initialLayout!=VK_IMAGE_LAYOUT_PREINITIALIZED))return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(60,KEY(d),info->format,info->extent.width,info->extent.height,info->usage,PTR(out),info->initialLayout,(stencil||external)?PTR(extra):extra[0],external?UINT64_MAX-1:stencil?UINT64_MAX:extra[1],info->flags,info->imageType,info->extent.depth,info->mipLevels,info->arrayLayers,info->samples,info->tiling);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice d,VkImage image,const VkAllocationCallbacks *allocator) {if(image)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(61,KEY(d),(uint64_t)image));}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice d,VkImage image,VkMemoryRequirements *out) {checked(CALL(62,KEY(d),(uint64_t)image,PTR(out)));}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice d,VkImage image,VkDeviceMemory memory,VkDeviceSize offset) {return CALL(63,KEY(d),(uint64_t)image,(uint64_t)memory,offset);}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice d,const VkImageViewCreateInfo *info,const VkAllocationCallbacks *allocator,VkImageView *out) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;ALLOCATOR(allocator);
    const VkImageViewUsageCreateInfo *usage=NULL;const VkSamplerYcbcrConversionInfo *conversion=NULL;
    for(const VkBaseInStructure *n=info->pNext;n;n=n->pNext){
        if(n->sType==VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO && !usage)usage=(const void *)n;
        else if(n->sType==VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO && !conversion)conversion=(const void *)n;
        else return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    const VkImageSubresourceRange *r=&info->subresourceRange;
    if(info->flags || info->viewType>VK_IMAGE_VIEW_TYPE_CUBE_ARRAY || info->components.r>VK_COMPONENT_SWIZZLE_A || info->components.g>VK_COMPONENT_SWIZZLE_A || info->components.b>VK_COMPONENT_SWIZZLE_A || info->components.a>VK_COMPONENT_SWIZZLE_A || !r->aspectMask || (r->aspectMask&~(format_aspects(info->format)|VK_IMAGE_ASPECT_PLANE_0_BIT|VK_IMAGE_ASPECT_PLANE_1_BIT|VK_IMAGE_ASPECT_PLANE_2_BIT)))return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t range[5]={r->aspectMask,r->baseMipLevel,r->levelCount,r->baseArrayLayer,r->layerCount};
    return CALL(64,KEY(d),(uint64_t)info->image,info->format,PTR(out),usage?usage->usage:0,info->viewType,(uint64_t)info->components.r|((uint64_t)info->components.g<<4)|((uint64_t)info->components.b<<8)|((uint64_t)info->components.a<<12),PTR(range),conversion?(uint64_t)conversion->conversion:0);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice d,VkImageView view,const VkAllocationCallbacks *allocator) {if(view)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(65,KEY(d),(uint64_t)view));}
// Single subpass: used colors in attachment order, sparse output locations, optional depth. The copied
// color array extends the original single-color wire without host pointers.
// Mesa 25's legacy render-pass builder can retain this layout even when its
// barrier path correctly falls back without EXT_attachment_feedback_loop_layout.
// Use the core GENERAL feedback path, without claiming the unimplemented EXT.
static VkImageLayout attachment_layout(VkImageLayout value) {
    return value==VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT?VK_IMAGE_LAYOUT_GENERAL:value;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice d,const VkRenderPassCreateInfo *info,const VkAllocationCallbacks *allocator,VkRenderPass *out) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;ALLOCATOR(allocator);
    return render_graph_legacy(d,info,out);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice d,VkRenderPass pass,const VkAllocationCallbacks *allocator) {if(pass)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(67,KEY(d),(uint64_t)pass));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice d,const VkFramebufferCreateInfo *info,const VkAllocationCallbacks *allocator,VkFramebuffer *out) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;ALLOCATOR(allocator);
    if(info->flags==VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT){
        const VkFramebufferAttachmentsCreateInfo *a=info->pNext;
        if(!VALID(a,VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO) || a->attachmentImageInfoCount!=info->attachmentCount || !info->layers)return VK_ERROR_FEATURE_NOT_PRESENT;
        uint64_t (*wire)[7]=info->attachmentCount?malloc((size_t)info->attachmentCount*sizeof(*wire)):NULL;if(info->attachmentCount && !wire)return VK_ERROR_OUT_OF_HOST_MEMORY;
        for(uint32_t i=0;i<info->attachmentCount;i++){
            const VkFramebufferAttachmentImageInfo *v=a->pAttachmentImageInfos+i;
            if(!VALID(v,VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO)){free(wire);return VK_ERROR_FEATURE_NOT_PRESENT;}
            wire[i][0]=v->flags;wire[i][1]=v->usage;wire[i][2]=v->width;wire[i][3]=v->height;wire[i][4]=v->layerCount;wire[i][5]=PTR(v->pViewFormats);wire[i][6]=v->viewFormatCount;
        }
        VkResult result=CALL(125,KEY(d),(uint64_t)info->renderPass,info->width,info->height,info->attachmentCount,PTR(wire),PTR(out),info->layers);free(wire);return result;
    }
    if(!supported_next(info->pNext))return VK_ERROR_FEATURE_NOT_PRESENT;
    if(info->flags || !info->layers)return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(68,KEY(d),(uint64_t)info->renderPass,0,info->width,info->height,PTR(out),0,info->pAttachments?PTR(info->pAttachments):PTR(info),info->attachmentCount,info->layers,1);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice d,VkFramebuffer fb,const VkAllocationCallbacks *allocator) {if(fb)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(69,KEY(d),(uint64_t)fb));}
static VkResult unsupported_graphics_pipeline(unsigned line) {
    if(getenv("TRAMES_VULKAN_TRACE"))fprintf(stderr,"Trames Vulkan: unsupported graphics pipeline state at graphics.inc:%u\n",line);
    return VK_ERROR_FEATURE_NOT_PRESENT;
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride) {checked(CALL(203,DEVKEY(c),KEY(c),(uint64_t)buffer,offset,count,stride));}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride) {checked(CALL(204,DEVKEY(c),KEY(c),(uint64_t)buffer,offset,count,stride));}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset) {checked(CALL(205,DEVKEY(c),KEY(c),(uint64_t)buffer,offset));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out) {
    ALLOCATOR(allocator);if(count!=1)return unsupported_graphics_pipeline(__LINE__);*out=VK_NULL_HANDLE;
    CHECK(info,VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    if(info->flags || info->stageCount<1 || info->stageCount>2 || info->pTessellationState || info->basePipelineHandle)return unsupported_graphics_pipeline(__LINE__);
    const VkPipelineShaderStageCreateInfo *vertex=NULL,*fragment=NULL;
    for(unsigned i=0;i<info->stageCount;i++){const VkPipelineShaderStageCreateInfo *s=&info->pStages[i];CHECK(s,VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);if(s->flags || s->pSpecializationInfo)return unsupported_graphics_pipeline(__LINE__);if(s->stage==VK_SHADER_STAGE_VERTEX_BIT && !vertex)vertex=s;else if(s->stage==VK_SHADER_STAGE_FRAGMENT_BIT && !fragment)fragment=s;else return unsupported_graphics_pipeline(__LINE__);}
    const VkPipelineVertexInputStateCreateInfo *v=info->pVertexInputState;if(!v || v->sType!=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO)return unsupported_graphics_pipeline(__LINE__);if(v->flags || v->vertexBindingDescriptionCount>16 || v->vertexAttributeDescriptionCount>16)return unsupported_graphics_pipeline(__LINE__);
    const VkPipelineInputAssemblyStateCreateInfo *a=info->pInputAssemblyState;CHECK(a,VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);if(a->flags || (a->primitiveRestartEnable && a->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP && a->topology!=VK_PRIMITIVE_TOPOLOGY_LINE_STRIP && a->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN))return unsupported_graphics_pipeline(__LINE__);
    const VkPipelineViewportStateCreateInfo *p=info->pViewportState;CHECK(p,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);if(p->flags || p->viewportCount!=1 || p->scissorCount!=1)return unsupported_graphics_pipeline(__LINE__);
    const VkPipelineRasterizationStateCreateInfo *r=info->pRasterizationState;CHECK(r,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);if(r->flags || r->depthClampEnable || r->polygonMode>VK_POLYGON_MODE_POINT || r->lineWidth!=1.0f)return unsupported_graphics_pipeline(__LINE__);
    if(!vertex || (!fragment && !r->rasterizerDiscardEnable))return unsupported_graphics_pipeline(__LINE__);
    const VkPipelineMultisampleStateCreateInfo *m=info->pMultisampleState;CHECK(m,VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    if(m->flags || !m->rasterizationSamples || m->rasterizationSamples>VK_SAMPLE_COUNT_64_BIT || (m->rasterizationSamples&(m->rasterizationSamples-1))){
        if(getenv("TRAMES_VULKAN_TRACE"))fprintf(stderr,"Trames Vulkan: multisample flags=%u samples=%u shading=%u coverage=%u alphaOne=%u mask=%08x\n",m->flags,m->rasterizationSamples,m->sampleShadingEnable,m->alphaToCoverageEnable,m->alphaToOneEnable,m->pSampleMask?m->pSampleMask[0]:UINT32_MAX);
        return unsupported_graphics_pipeline(__LINE__);
    }
    const VkPipelineDepthStencilStateCreateInfo *z=info->pDepthStencilState;if(z){CHECK(z,VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);if(z->flags || z->depthBoundsTestEnable || z->depthCompareOp>VK_COMPARE_OP_ALWAYS)return unsupported_graphics_pipeline(__LINE__);}
    const VkPipelineColorBlendStateCreateInfo *b=info->pColorBlendState;CHECK(b,VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);if(b->flags || b->logicOp>VK_LOGIC_OP_SET || b->attachmentCount>8)return unsupported_graphics_pipeline(__LINE__);
    const VkPipelineDynamicStateCreateInfo *dynamic=info->pDynamicState;unsigned states=0;
    if(dynamic){CHECK(dynamic,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);if(dynamic->flags || dynamic->dynamicStateCount>9)return unsupported_graphics_pipeline(__LINE__);for(unsigned i=0;i<dynamic->dynamicStateCount;i++){unsigned bit=dynamic->pDynamicStates[i]<=VK_DYNAMIC_STATE_STENCIL_REFERENCE?1u<<dynamic->pDynamicStates[i]:0;if(!bit || (states&bit))return unsupported_graphics_pipeline(__LINE__);states|=bit;}}
    if(r->depthBiasEnable && !(states&(1u<<VK_DYNAMIC_STATE_DEPTH_BIAS)) && (r->depthBiasConstantFactor!=0 || r->depthBiasClamp!=0 || r->depthBiasSlopeFactor!=0))return unsupported_graphics_pipeline(__LINE__);
    uint64_t config[23+8*8+15]={states|512|1024|2048|4096|16384|32768};config[22]=b->attachmentCount;
    // Copy the original sample masks/state before asynchronous native compile.
    config[11]=m->pSampleMask?m->pSampleMask[0]:UINT32_MAX;
    config[13]=m->rasterizationSamples;config[14]=m->pSampleMask && m->rasterizationSamples==VK_SAMPLE_COUNT_64_BIT?m->pSampleMask[1]:UINT32_MAX;
    config[15]=m->alphaToCoverageEnable;config[16]=m->alphaToOneEnable;
    if(m->sampleShadingEnable){uint32_t bits;memcpy(&bits,&m->minSampleShading,4);config[0]|=8192;config[12]=bits;}
    if(z){
        uint64_t *st=config+23+8*8;st[0]=z->stencilTestEnable;
        const VkStencilOpState *faces[2]={&z->front,&z->back};
        for(unsigned i=0;i<2;i++){const VkStencilOpState *f=faces[i];uint64_t *v=st+1+i*7;
            v[0]=f->failOp;v[1]=f->passOp;v[2]=f->depthFailOp;v[3]=f->compareOp;v[4]=f->compareMask;v[5]=f->writeMask;v[6]=f->reference;
        }
    }
    if(!(states&1)){if(!p->pViewports)return unsupported_graphics_pipeline(__LINE__);uint32_t bits[6];memcpy(bits,p->pViewports,sizeof(bits));for(unsigned i=0;i<6;i++)config[1+i]=bits[i];}
    if(!(states&2)){if(!p->pScissors)return unsupported_graphics_pipeline(__LINE__);config[7]=(uint64_t)(int64_t)p->pScissors[0].offset.x;config[8]=(uint64_t)(int64_t)p->pScissors[0].offset.y;config[9]=p->pScissors[0].extent.width;config[10]=p->pScissors[0].extent.height;}
    for(unsigned i=0;i<b->attachmentCount;i++){
        const VkPipelineColorBlendAttachmentState *ba=b->pAttachments+i;uint64_t *v=config+23+i*8;
        v[0]=ba->blendEnable;v[1]=ba->srcColorBlendFactor;v[2]=ba->dstColorBlendFactor;v[3]=ba->colorBlendOp;v[4]=ba->srcAlphaBlendFactor;v[5]=ba->dstAlphaBlendFactor;v[6]=ba->alphaBlendOp;v[7]=ba->colorWriteMask;
    }
    uint32_t constants[4];memcpy(constants,b->blendConstants,sizeof(constants));for(unsigned i=0;i<4;i++)config[18+i]=constants[i];
    uint64_t state[6]={(uint64_t)info->renderPass,a->topology,r->cullMode,r->frontFace,b->attachmentCount?b->pAttachments[0].colorWriteMask:0,info->subpass};
    uint64_t bindings[16][4],attributes[16][4];
    for(uint32_t i=0;i<v->vertexBindingDescriptionCount;i++){const VkVertexInputBindingDescription *x=&v->pVertexBindingDescriptions[i];bindings[i][0]=x->binding;bindings[i][1]=x->stride;bindings[i][2]=x->inputRate;bindings[i][3]=1;}
    if(v->pNext){
        const VkPipelineVertexInputDivisorStateCreateInfoEXT *ds=v->pNext;
        if(!(D(d)->enabled&32768) || !VALID(ds,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT) || !ds->vertexBindingDivisorCount || ds->vertexBindingDivisorCount>16 || !ds->pVertexBindingDivisors)return unsupported_graphics_pipeline(__LINE__);
        unsigned seen=0;
        for(uint32_t i=0;i<ds->vertexBindingDivisorCount;i++){
            const VkVertexInputBindingDivisorDescriptionEXT *x=ds->pVertexBindingDivisors+i;unsigned at=0;
            while(at<v->vertexBindingDescriptionCount && bindings[at][0]!=x->binding)at++;
            if(at==v->vertexBindingDescriptionCount || bindings[at][2]!=VK_VERTEX_INPUT_RATE_INSTANCE || (seen&(1u<<at)))return unsupported_graphics_pipeline(__LINE__);
            seen|=1u<<at;bindings[at][3]=x->divisor;
        }
    }
    for(uint32_t i=0;i<v->vertexAttributeDescriptionCount;i++){const VkVertexInputAttributeDescription *x=&v->pVertexAttributeDescriptions[i];attributes[i][0]=x->location;attributes[i][1]=x->binding;attributes[i][2]=x->format;attributes[i][3]=x->offset;}
    return CALL(70,KEY(d),(uint64_t)info->layout,(uint64_t)vertex->module,fragment?(uint64_t)fragment->module:0,PTR(vertex->pName),fragment?PTR(fragment->pName):0,PTR(state),PTR(out),PTR(bindings),v->vertexBindingDescriptionCount,PTR(attributes),v->vertexAttributeDescriptionCount,a->primitiveRestartEnable,(uint64_t)cache,((z?((z->depthTestEnable?1UL:0)|(z->depthWriteEnable?2UL:0)|((uint64_t)z->depthCompareOp<<4)):0)|((uint64_t)r->polygonMode<<8)|(b->logicOpEnable?1024UL:0)|((uint64_t)b->logicOp<<12)|(r->rasterizerDiscardEnable?65536UL:0)|(r->depthBiasEnable?131072UL:0)),PTR(config));
}
static VkResult begin_pass(VkCommandBuffer c,const VkRenderPassBeginInfo *info,VkSubpassContents contents,unsigned op) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO || contents>VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkRenderPassAttachmentBeginInfo *a=info->pNext;
    if(a && (!VALID(a,VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO)))return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(op,DEVKEY(c),KEY(c),(uint64_t)info->renderPass,(uint64_t)info->framebuffer,(uint64_t)(int64_t)info->renderArea.offset.x,(uint64_t)(int64_t)info->renderArea.offset.y,info->renderArea.extent.width,info->renderArea.extent.height,PTR(info->pClearValues),info->clearValueCount,a?PTR(a->pAttachments):0,a?a->attachmentCount:0,contents);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c,const VkRenderPassBeginInfo *info,VkSubpassContents contents) {checked(begin_pass(c,info,contents,71));}
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(VkCommandBuffer c,VkSubpassContents contents) {checked(CALL(218,DEVKEY(c),KEY(c),contents,0));}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c) {checked(CALL(72,DEVKEY(c),KEY(c)));}
VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer c,uint32_t count,const VkClearAttachment *attachments,uint32_t rect_count,const VkClearRect *rects) {
    if(!count || count>9 || !attachments || !rect_count || rect_count>64 || !rects)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    uint64_t values[9][6],regions[64][6];
    for(uint32_t i=0;i<count;i++){
        values[i][0]=attachments[i].aspectMask;values[i][1]=attachments[i].colorAttachment;
        uint32_t bits[4]={0};memcpy(bits,&attachments[i].clearValue,attachments[i].aspectMask==VK_IMAGE_ASPECT_COLOR_BIT?sizeof(bits):2*sizeof(uint32_t));
        for(unsigned j=0;j<4;j++)values[i][j+2]=bits[j];
    }
    for(uint32_t i=0;i<rect_count;i++){
        regions[i][0]=(uint64_t)(int64_t)rects[i].rect.offset.x;regions[i][1]=(uint64_t)(int64_t)rects[i].rect.offset.y;
        regions[i][2]=rects[i].rect.extent.width;regions[i][3]=rects[i].rect.extent.height;
        regions[i][4]=rects[i].baseArrayLayer;regions[i][5]=rects[i].layerCount;
    }
    checked(CALL(151,DEVKEY(c),KEY(c),PTR(values),count,PTR(regions),rect_count));
}
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer c,uint32_t vertices,uint32_t instances,uint32_t firstVertex,uint32_t firstInstance) {checked(CALL(73,DEVKEY(c),KEY(c),vertices,instances,firstVertex,firstInstance));}
static void buffer_image_copy(VkCommandBuffer c,VkImage image,VkImageLayout layout,VkBuffer buffer,uint32_t count,const VkBufferImageCopy *regions,unsigned op) {
    if(count>64)checked(VK_ERROR_FEATURE_NOT_PRESENT);uint64_t data[64][13];
    for(uint32_t i=0;i<count;i++){const VkBufferImageCopy *x=&regions[i];uint64_t *v=data[i];v[0]=x->bufferOffset;v[1]=x->bufferRowLength;v[2]=x->bufferImageHeight;v[3]=x->imageSubresource.aspectMask;v[4]=x->imageSubresource.mipLevel;v[5]=x->imageSubresource.baseArrayLayer;v[6]=x->imageSubresource.layerCount;v[7]=x->imageOffset.x;v[8]=x->imageOffset.y;v[9]=x->imageOffset.z;v[10]=x->imageExtent.width;v[11]=x->imageExtent.height;v[12]=x->imageExtent.depth;}
    checked(CALL(op,DEVKEY(c),KEY(c),(uint64_t)image,layout,(uint64_t)buffer,PTR(data),count));
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer c,uint32_t first,uint32_t count,const VkViewport *viewports) {if(first || count!=1)checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(75,DEVKEY(c),KEY(c),PTR(viewports)));}
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer c,uint32_t first,uint32_t count,const VkRect2D *scissors) {if(first || count!=1)checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(76,DEVKEY(c),KEY(c),scissors[0].offset.x,scissors[0].offset.y,scissors[0].extent.width,scissors[0].extent.height));}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer c,VkImage image,VkImageLayout layout,VkBuffer buffer,uint32_t count,const VkBufferImageCopy *regions) {buffer_image_copy(c,image,layout,buffer,count,regions,74);}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer c,VkBuffer buffer,VkImage image,VkImageLayout layout,uint32_t count,const VkBufferImageCopy *regions) {buffer_image_copy(c,image,layout,buffer,count,regions,94);}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer c,VkImage src,VkImageLayout src_layout,VkImage dst,VkImageLayout dst_layout,uint32_t count,const VkImageCopy *regions) {
    if(!count || count>64 || !regions)checked(VK_ERROR_FEATURE_NOT_PRESENT);uint64_t data[64][17];
    for(uint32_t i=0;i<count;i++){
        const VkImageCopy *r=&regions[i];uint64_t *v=data[i];
        v[0]=r->srcSubresource.aspectMask;v[1]=r->srcSubresource.mipLevel;v[2]=r->srcSubresource.baseArrayLayer;v[3]=r->srcSubresource.layerCount;
        v[4]=(int64_t)r->srcOffset.x;v[5]=(int64_t)r->srcOffset.y;v[6]=(int64_t)r->srcOffset.z;
        v[7]=r->dstSubresource.aspectMask;v[8]=r->dstSubresource.mipLevel;v[9]=r->dstSubresource.baseArrayLayer;v[10]=r->dstSubresource.layerCount;
        v[11]=(int64_t)r->dstOffset.x;v[12]=(int64_t)r->dstOffset.y;v[13]=(int64_t)r->dstOffset.z;
        v[14]=r->extent.width;v[15]=r->extent.height;v[16]=r->extent.depth;
    }
    checked(CALL(134,DEVKEY(c),KEY(c),(uint64_t)src,src_layout,(uint64_t)dst,dst_layout,PTR(data),count));
}
VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer c,VkImage src,VkImageLayout src_layout,VkImage dst,VkImageLayout dst_layout,uint32_t count,const VkImageResolve *regions) {
    if(!count || count>64 || !regions)checked(VK_ERROR_FEATURE_NOT_PRESENT);uint64_t data[64][17];
    for(uint32_t i=0;i<count;i++){
        const VkImageResolve *r=&regions[i];uint64_t *v=data[i];
        v[0]=r->srcSubresource.aspectMask;v[1]=r->srcSubresource.mipLevel;v[2]=r->srcSubresource.baseArrayLayer;v[3]=r->srcSubresource.layerCount;
        v[4]=(int64_t)r->srcOffset.x;v[5]=(int64_t)r->srcOffset.y;v[6]=(int64_t)r->srcOffset.z;
        v[7]=r->dstSubresource.aspectMask;v[8]=r->dstSubresource.mipLevel;v[9]=r->dstSubresource.baseArrayLayer;v[10]=r->dstSubresource.layerCount;
        v[11]=(int64_t)r->dstOffset.x;v[12]=(int64_t)r->dstOffset.y;v[13]=(int64_t)r->dstOffset.z;
        v[14]=r->extent.width;v[15]=r->extent.height;v[16]=r->extent.depth;
    }
    checked(CALL(215,DEVKEY(c),KEY(c),(uint64_t)src,src_layout,(uint64_t)dst,dst_layout,PTR(data),count));
}
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer c,VkImage src,VkImageLayout src_layout,VkImage dst,VkImageLayout dst_layout,uint32_t count,const VkImageBlit *regions,VkFilter filter) {
    if(!count || count>64 || !regions)checked(VK_ERROR_FEATURE_NOT_PRESENT);uint64_t data[64][20];
    for(uint32_t i=0;i<count;i++){
        const VkImageBlit *r=regions+i;uint64_t *v=data[i];
        v[0]=r->srcSubresource.aspectMask;v[1]=r->srcSubresource.mipLevel;v[2]=r->srcSubresource.baseArrayLayer;v[3]=r->srcSubresource.layerCount;
        v[10]=r->dstSubresource.aspectMask;v[11]=r->dstSubresource.mipLevel;v[12]=r->dstSubresource.baseArrayLayer;v[13]=r->dstSubresource.layerCount;
        for(unsigned j=0;j<2;j++){v[4+j*3]=(int64_t)r->srcOffsets[j].x;v[5+j*3]=(int64_t)r->srcOffsets[j].y;v[6+j*3]=(int64_t)r->srcOffsets[j].z;
            v[14+j*3]=(int64_t)r->dstOffsets[j].x;v[15+j*3]=(int64_t)r->dstOffsets[j].y;v[16+j*3]=(int64_t)r->dstOffsets[j].z;}
    }
    checked(CALL(135,DEVKEY(c),KEY(c),(uint64_t)src,src_layout,(uint64_t)dst,dst_layout,PTR(data),count,filter));
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer c,VkImage image,VkImageLayout layout,const VkClearDepthStencilValue *clear,uint32_t count,const VkImageSubresourceRange *ranges) {
    if(!count || count>64 || !clear || !ranges)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    uint64_t wire[64][5];for(uint32_t i=0;i<count;i++){const VkImageSubresourceRange *r=ranges+i;wire[i][0]=r->aspectMask;wire[i][1]=r->baseMipLevel;wire[i][2]=r->levelCount;wire[i][3]=r->baseArrayLayer;wire[i][4]=r->layerCount;}
    checked(CALL(136,DEVKEY(c),KEY(c),(uint64_t)image,layout,PTR(clear),PTR(wire),count));
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice d,const VkSamplerCreateInfo *info,const VkAllocationCallbacks *allocator,VkSampler *out) {
    if(!info || info->sType!=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO)return VK_ERROR_FEATURE_NOT_PRESENT;ALLOCATOR(allocator);
    const VkSamplerReductionModeCreateInfo *reduction=NULL;const VkSamplerYcbcrConversionInfo *conversion=NULL;
    for(const VkBaseInStructure *n=info->pNext;n;n=n->pNext){
        if(n->sType==VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO && !reduction)reduction=(const void *)n;
        else if(n->sType==VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO && !conversion)conversion=(const void *)n;
        else return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if(info->flags || info->unnormalizedCoordinates)return VK_ERROR_FEATURE_NOT_PRESENT;
    float lod[3]={info->mipLodBias,info->minLod,info->maxLod};uint32_t anisotropy;memcpy(&anisotropy,&info->maxAnisotropy,4);
    return CALL(92,KEY(d),info->magFilter,info->minFilter,info->mipmapMode,info->addressModeU,info->addressModeV,info->addressModeW,info->borderColor,PTR(lod),PTR(out),info->anisotropyEnable,anisotropy,info->compareEnable,info->compareOp,reduction?reduction->reductionMode:0,conversion?(uint64_t)conversion->conversion:0);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice d,VkSampler sampler,const VkAllocationCallbacks *allocator) {if(sampler)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(93,KEY(d),(uint64_t)sampler));}
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer c,uint32_t first,uint32_t count,const VkBuffer *buffers,const VkDeviceSize *offsets) {
    if(count>16)checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(95,DEVKEY(c),KEY(c),first,count,PTR(buffers),PTR(offsets)));
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,VkIndexType type) {checked(CALL(96,DEVKEY(c),KEY(c),(uint64_t)buffer,offset,type));}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer c,uint32_t indices,uint32_t instances,uint32_t firstIndex,int32_t vertexOffset,uint32_t firstInstance) {checked(CALL(97,DEVKEY(c),KEY(c),indices,instances,firstIndex,(uint64_t)(int64_t)vertexOffset,firstInstance));}

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer c,VkImage image,VkImageLayout layout,const VkClearColorValue *color,uint32_t count,const VkImageSubresourceRange *ranges) {
    if(!count || count>64 || !color || !ranges)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    uint64_t wire[64][5];for(uint32_t i=0;i<count;i++){const VkImageSubresourceRange *r=ranges+i;wire[i][0]=r->aspectMask;wire[i][1]=r->baseMipLevel;wire[i][2]=r->levelCount;wire[i][3]=r->baseArrayLayer;wire[i][4]=r->layerCount;}
    checked(CALL(114,DEVKEY(c),KEY(c),(uint64_t)image,layout,PTR(color),PTR(wire),count));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(VkDevice d,const VkBufferViewCreateInfo *info,const VkAllocationCallbacks *allocator,VkBufferView *out) {
    CHECK(info,VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO);ALLOCATOR(allocator);if(info->flags)return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(127,KEY(d),(uint64_t)info->buffer,info->format,info->offset,info->range,PTR(out));
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(VkDevice d,VkBufferView view,const VkAllocationCallbacks *allocator) {if(view)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(128,KEY(d),(uint64_t)view));}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer c,float value) {uint32_t bits;memcpy(&bits,&value,4);checked(CALL(132,DEVKEY(c),KEY(c),VK_DYNAMIC_STATE_LINE_WIDTH,PTR(&bits),1));}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer c,float constant,float clamp,float slope) {float values[3]={constant,clamp,slope};checked(CALL(132,DEVKEY(c),KEY(c),VK_DYNAMIC_STATE_DEPTH_BIAS,PTR(values),3));}
VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer c,const float values[4]) {checked(CALL(132,DEVKEY(c),KEY(c),VK_DYNAMIC_STATE_BLEND_CONSTANTS,PTR(values),4));}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(VkCommandBuffer c,float min,float max) {float values[2]={min,max};checked(CALL(132,DEVKEY(c),KEY(c),VK_DYNAMIC_STATE_DEPTH_BOUNDS,PTR(values),2));}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer c,VkStencilFaceFlags face,uint32_t mask) {uint32_t values[2]={face,mask};checked(CALL(132,DEVKEY(c),KEY(c),VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,PTR(values),2));}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer c,VkStencilFaceFlags face,uint32_t mask) {uint32_t values[2]={face,mask};checked(CALL(132,DEVKEY(c),KEY(c),VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,PTR(values),2));}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer c,VkStencilFaceFlags face,uint32_t reference) {uint32_t values[2]={face,reference};checked(CALL(132,DEVKEY(c),KEY(c),VK_DYNAMIC_STATE_STENCIL_REFERENCE,PTR(values),2));}

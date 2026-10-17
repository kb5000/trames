// SPDX-License-Identifier: GPL-3.0-or-later
// Core graphics frontend: single color attachment, vertex/index input, sampled textures, dynamic
// static/dynamic viewport/scissor, depth and native blend/sampler state.
// Wire op 58..76, 92..97 and 114.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice d,const VkImageCreateInfo *info,const VkAllocationCallbacks *allocator,VkImage *out) {
    CHECK(info,VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);ALLOCATOR(allocator);
    if(info->flags || info->imageType!=VK_IMAGE_TYPE_2D || info->extent.depth!=1 || info->mipLevels!=1 || info->arrayLayers!=1 || info->samples!=VK_SAMPLE_COUNT_1_BIT || info->tiling!=VK_IMAGE_TILING_OPTIMAL || info->sharingMode!=VK_SHARING_MODE_EXCLUSIVE || (info->initialLayout!=VK_IMAGE_LAYOUT_UNDEFINED && info->initialLayout!=VK_IMAGE_LAYOUT_PREINITIALIZED))return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(60,KEY(d),info->format,info->extent.width,info->extent.height,info->usage,PTR(out),info->initialLayout);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice d,VkImage image,const VkAllocationCallbacks *allocator) {if(image)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(61,KEY(d),(uint64_t)image));}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice d,VkImage image,VkMemoryRequirements *out) {checked(CALL(62,KEY(d),(uint64_t)image,PTR(out)));}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice d,VkImage image,VkDeviceMemory memory,VkDeviceSize offset) {return CALL(63,KEY(d),(uint64_t)image,(uint64_t)memory,offset);}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice d,const VkImageViewCreateInfo *info,const VkAllocationCallbacks *allocator,VkImageView *out) {
    CHECK(info,VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);ALLOCATOR(allocator);
    const VkImageSubresourceRange *r=&info->subresourceRange;
    if(info->flags || info->viewType!=VK_IMAGE_VIEW_TYPE_2D || info->components.r || info->components.g || info->components.b || info->components.a || r->aspectMask!=((info->format==VK_FORMAT_D16_UNORM||info->format==VK_FORMAT_D32_SFLOAT)?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT) || r->baseMipLevel || r->levelCount!=1 || r->baseArrayLayer || r->layerCount!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(64,KEY(d),(uint64_t)info->image,info->format,PTR(out));
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice d,VkImageView view,const VkAllocationCallbacks *allocator) {if(view)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(65,KEY(d),(uint64_t)view));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice d,const VkRenderPassCreateInfo *info,const VkAllocationCallbacks *allocator,VkRenderPass *out) {
    CHECK(info,VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);ALLOCATOR(allocator);
    if(info->flags || (info->attachmentCount!=1 && info->attachmentCount!=2) || info->subpassCount!=1 || info->dependencyCount>8)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkAttachmentDescription *a=info->pAttachments;const VkSubpassDescription *s=info->pSubpasses;
    if(a->flags || a->samples!=VK_SAMPLE_COUNT_1_BIT || s->flags || s->pipelineBindPoint!=VK_PIPELINE_BIND_POINT_GRAPHICS || s->inputAttachmentCount || s->colorAttachmentCount!=1 || s->pColorAttachments[0].attachment!=0 || s->pColorAttachments[0].layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || s->pResolveAttachments || s->preserveAttachmentCount)return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t depth[5];
    if(info->attachmentCount==2){
        const VkAttachmentDescription *z=a+1;
        if(!s->pDepthStencilAttachment || s->pDepthStencilAttachment->attachment!=1 || s->pDepthStencilAttachment->layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || z->flags || (z->format!=VK_FORMAT_D16_UNORM && z->format!=VK_FORMAT_D32_SFLOAT) || z->samples!=VK_SAMPLE_COUNT_1_BIT)return VK_ERROR_FEATURE_NOT_PRESENT;
        depth[0]=z->format;depth[1]=z->loadOp;depth[2]=z->storeOp;depth[3]=z->initialLayout;depth[4]=z->finalLayout;
    } else if(s->pDepthStencilAttachment && s->pDepthStencilAttachment->attachment!=VK_ATTACHMENT_UNUSED)return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t dependencies[8][7];for(uint32_t i=0;i<info->dependencyCount;i++){const VkSubpassDependency *x=&info->pDependencies[i];dependencies[i][0]=x->srcSubpass;dependencies[i][1]=x->dstSubpass;dependencies[i][2]=x->srcStageMask;dependencies[i][3]=x->dstStageMask;dependencies[i][4]=x->srcAccessMask;dependencies[i][5]=x->dstAccessMask;dependencies[i][6]=x->dependencyFlags;}
    return CALL(66,KEY(d),a->format,a->loadOp,a->storeOp,a->initialLayout,a->finalLayout,PTR(dependencies),info->dependencyCount,PTR(out),info->attachmentCount==2?PTR(depth):0);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice d,VkRenderPass pass,const VkAllocationCallbacks *allocator) {if(pass)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(67,KEY(d),(uint64_t)pass));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice d,const VkFramebufferCreateInfo *info,const VkAllocationCallbacks *allocator,VkFramebuffer *out) {
    CHECK(info,VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);ALLOCATOR(allocator);
    if(info->flags || (info->attachmentCount!=1 && info->attachmentCount!=2) || info->layers!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(68,KEY(d),(uint64_t)info->renderPass,(uint64_t)info->pAttachments[0],info->width,info->height,PTR(out),info->attachmentCount==2?(uint64_t)info->pAttachments[1]:0);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice d,VkFramebuffer fb,const VkAllocationCallbacks *allocator) {if(fb)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(69,KEY(d),(uint64_t)fb));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out) {
    ALLOCATOR(allocator);if(count!=1)return VK_ERROR_FEATURE_NOT_PRESENT;*out=VK_NULL_HANDLE;
    CHECK(info,VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    if(info->flags || info->stageCount!=2 || info->pTessellationState || info->subpass || info->basePipelineHandle)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkPipelineShaderStageCreateInfo *vertex=NULL,*fragment=NULL;
    for(unsigned i=0;i<2;i++){const VkPipelineShaderStageCreateInfo *s=&info->pStages[i];CHECK(s,VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);if(s->flags || s->pSpecializationInfo)return VK_ERROR_FEATURE_NOT_PRESENT;if(s->stage==VK_SHADER_STAGE_VERTEX_BIT && !vertex)vertex=s;else if(s->stage==VK_SHADER_STAGE_FRAGMENT_BIT && !fragment)fragment=s;else return VK_ERROR_FEATURE_NOT_PRESENT;}
    const VkPipelineVertexInputStateCreateInfo *v=info->pVertexInputState;CHECK(v,VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);if(v->flags || v->vertexBindingDescriptionCount>16 || v->vertexAttributeDescriptionCount>16)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkPipelineInputAssemblyStateCreateInfo *a=info->pInputAssemblyState;CHECK(a,VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);if(a->flags || (a->primitiveRestartEnable && a->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP))return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkPipelineViewportStateCreateInfo *p=info->pViewportState;CHECK(p,VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);if(p->flags || p->viewportCount!=1 || p->scissorCount!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkPipelineRasterizationStateCreateInfo *r=info->pRasterizationState;CHECK(r,VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);if(r->flags || r->depthClampEnable || r->rasterizerDiscardEnable || r->polygonMode!=VK_POLYGON_MODE_FILL || r->depthBiasEnable || r->lineWidth!=1.0f)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkPipelineMultisampleStateCreateInfo *m=info->pMultisampleState;CHECK(m,VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);if(m->flags || m->rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT || m->sampleShadingEnable || m->alphaToCoverageEnable || m->alphaToOneEnable || (m->pSampleMask && m->pSampleMask[0]!=UINT32_MAX))return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkPipelineDepthStencilStateCreateInfo *z=info->pDepthStencilState;if(z){CHECK(z,VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);if(z->flags || z->depthBoundsTestEnable || z->stencilTestEnable || z->depthCompareOp>VK_COMPARE_OP_ALWAYS)return VK_ERROR_FEATURE_NOT_PRESENT;}
    const VkPipelineColorBlendStateCreateInfo *b=info->pColorBlendState;CHECK(b,VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);if(b->flags || b->logicOpEnable || b->attachmentCount!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkPipelineDynamicStateCreateInfo *dynamic=info->pDynamicState;unsigned states=0;
    if(dynamic){CHECK(dynamic,VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);if(dynamic->flags || dynamic->dynamicStateCount>2)return VK_ERROR_FEATURE_NOT_PRESENT;for(unsigned i=0;i<dynamic->dynamicStateCount;i++){unsigned bit=dynamic->pDynamicStates[i]==VK_DYNAMIC_STATE_VIEWPORT?1:dynamic->pDynamicStates[i]==VK_DYNAMIC_STATE_SCISSOR?2:0;if(!bit || (states&bit))return VK_ERROR_FEATURE_NOT_PRESENT;states|=bit;}}
    uint64_t config[22]={states};
    if(!(states&1)){if(!p->pViewports)return VK_ERROR_FEATURE_NOT_PRESENT;uint32_t bits[6];memcpy(bits,p->pViewports,sizeof(bits));for(unsigned i=0;i<6;i++)config[1+i]=bits[i];}
    if(!(states&2)){if(!p->pScissors)return VK_ERROR_FEATURE_NOT_PRESENT;config[7]=(uint64_t)(int64_t)p->pScissors[0].offset.x;config[8]=(uint64_t)(int64_t)p->pScissors[0].offset.y;config[9]=p->pScissors[0].extent.width;config[10]=p->pScissors[0].extent.height;}
    const VkPipelineColorBlendAttachmentState *ba=b->pAttachments;config[11]=ba->blendEnable;config[12]=ba->srcColorBlendFactor;config[13]=ba->dstColorBlendFactor;config[14]=ba->colorBlendOp;config[15]=ba->srcAlphaBlendFactor;config[16]=ba->dstAlphaBlendFactor;config[17]=ba->alphaBlendOp;
    uint32_t constants[4];memcpy(constants,b->blendConstants,sizeof(constants));for(unsigned i=0;i<4;i++)config[18+i]=constants[i];
    uint64_t state[5]={(uint64_t)info->renderPass,a->topology,r->cullMode,r->frontFace,b->pAttachments[0].colorWriteMask};
    uint64_t bindings[16][3],attributes[16][4];
    for(uint32_t i=0;i<v->vertexBindingDescriptionCount;i++){const VkVertexInputBindingDescription *x=&v->pVertexBindingDescriptions[i];bindings[i][0]=x->binding;bindings[i][1]=x->stride;bindings[i][2]=x->inputRate;}
    for(uint32_t i=0;i<v->vertexAttributeDescriptionCount;i++){const VkVertexInputAttributeDescription *x=&v->pVertexAttributeDescriptions[i];attributes[i][0]=x->location;attributes[i][1]=x->binding;attributes[i][2]=x->format;attributes[i][3]=x->offset;}
    return CALL(70,KEY(d),(uint64_t)info->layout,(uint64_t)vertex->module,(uint64_t)fragment->module,PTR(vertex->pName),PTR(fragment->pName),PTR(state),PTR(out),PTR(bindings),v->vertexBindingDescriptionCount,PTR(attributes),v->vertexAttributeDescriptionCount,a->primitiveRestartEnable,(uint64_t)cache,z?((z->depthTestEnable?1UL:0)|(z->depthWriteEnable?2UL:0)|((uint64_t)z->depthCompareOp<<4)):0,PTR(config));
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c,const VkRenderPassBeginInfo *info,VkSubpassContents contents) {
    if(!VALID(info,VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO) || contents!=VK_SUBPASS_CONTENTS_INLINE || info->clearValueCount>2)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    checked(CALL(71,DEVKEY(c),KEY(c),(uint64_t)info->renderPass,(uint64_t)info->framebuffer,info->renderArea.offset.x,info->renderArea.offset.y,info->renderArea.extent.width,info->renderArea.extent.height,PTR(info->pClearValues),info->clearValueCount));
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c) {checked(CALL(72,DEVKEY(c),KEY(c)));}
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
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice d,const VkSamplerCreateInfo *info,const VkAllocationCallbacks *allocator,VkSampler *out) {
    CHECK(info,VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);ALLOCATOR(allocator);
    if(info->flags || info->compareEnable || info->unnormalizedCoordinates)return VK_ERROR_FEATURE_NOT_PRESENT;
    float lod[3]={info->mipLodBias,info->minLod,info->maxLod};uint32_t anisotropy;memcpy(&anisotropy,&info->maxAnisotropy,4);
    return CALL(92,KEY(d),info->magFilter,info->minFilter,info->mipmapMode,info->addressModeU,info->addressModeV,info->addressModeW,info->borderColor,PTR(lod),PTR(out),info->anisotropyEnable,anisotropy);
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

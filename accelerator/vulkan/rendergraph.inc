// SPDX-License-Identifier: GPL-3.0-or-later
// Normalize both renderpass generations to one copied attachment/subpass graph.
// All pointers here still belong to the guest; the host builds owned native records.
typedef struct rg_block {struct rg_block *next;uint64_t words[];} RGBlock;
typedef struct {RGBlock *blocks;VkResult result;} RGArena;
static uint64_t *rg_alloc(RGArena *a,size_t count,size_t width){
    if(!count)return NULL;
    if(count> (SIZE_MAX-sizeof(RGBlock))/(width*8)){a->result=VK_ERROR_OUT_OF_HOST_MEMORY;return NULL;}
    RGBlock *b=calloc(1,sizeof(*b)+count*width*8);if(!b){a->result=VK_ERROR_OUT_OF_HOST_MEMORY;return NULL;}
    b->next=a->blocks;a->blocks=b;return b->words;
}
static VkResult rg_finish(RGArena *a,VkDevice d,uint64_t header[8],VkRenderPassCreateFlags flags,VkRenderPass *out){
    VkResult r=a->result;if(r==VK_SUCCESS)r=CALL(217,KEY(d),PTR(header),PTR(out),flags);
    while(a->blocks){RGBlock *next=a->blocks->next;free(a->blocks);a->blocks=next;}
    return r;
}
static uint64_t *rg_refs1(RGArena *a,uint32_t count,const VkAttachmentReference *refs){
    uint64_t *v=rg_alloc(a,count,4);if(!v)return NULL;
    for(uint32_t i=0;i<count;i++){v[i*4]=refs[i].attachment;v[i*4+1]=attachment_layout(refs[i].layout);v[i*4+2]=0;v[i*4+3]=UINT32_MAX;}
    return v;
}
static uint64_t *rg_refs2(RGArena *a,uint32_t count,const VkAttachmentReference2 *refs){
    uint64_t *v=rg_alloc(a,count,4);if(!v)return NULL;
    for(uint32_t i=0;i<count;i++){
        const VkAttachmentReference2 *r=refs+i;
        if(r->sType!=VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2){a->result=VK_ERROR_FEATURE_NOT_PRESENT;return NULL;}
        v[i*4]=r->attachment;v[i*4+1]=attachment_layout(r->layout);v[i*4+2]=r->aspectMask;v[i*4+3]=UINT32_MAX;
        const VkAttachmentReferenceStencilLayout *st=r->pNext;
        if(st){if(!VALID(st,VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_STENCIL_LAYOUT)){a->result=VK_ERROR_FEATURE_NOT_PRESENT;return NULL;}v[i*4+3]=st->stencilLayout;}
    }return v;
}
static VkResult render_graph_legacy(VkDevice d,const VkRenderPassCreateInfo *info,VkRenderPass *out){
    RGArena a={0};uint64_t h[8]={info->attachmentCount,0,info->subpassCount,0,info->dependencyCount,0,0,0};
    uint64_t *at=rg_alloc(&a,info->attachmentCount,11),*sub=rg_alloc(&a,info->subpassCount,14),*dep=rg_alloc(&a,info->dependencyCount,8);
    h[1]=PTR(at);h[3]=PTR(sub);h[5]=PTR(dep);if(a.result!=VK_SUCCESS)return rg_finish(&a,d,h,info->flags,out);
    const VkRenderPassMultiviewCreateInfo *mv=NULL;const VkRenderPassInputAttachmentAspectCreateInfo *aspects=NULL;
    const VkBaseInStructure *node=info->pNext;unsigned nodes=0;
    for(;node && nodes++<64;node=node->pNext){
        if(node->sType==VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO && !mv)mv=(const void *)node;
        else if(node->sType==VK_STRUCTURE_TYPE_RENDER_PASS_INPUT_ATTACHMENT_ASPECT_CREATE_INFO && !aspects)aspects=(const void *)node;
        else {a.result=VK_ERROR_FEATURE_NOT_PRESENT;break;}
    }
    if(node || (mv && ((mv->subpassCount && mv->subpassCount!=info->subpassCount) || (mv->dependencyCount && mv->dependencyCount!=info->dependencyCount))))a.result=VK_ERROR_FEATURE_NOT_PRESENT;
    if(mv){h[6]=mv->correlationMaskCount;h[7]=PTR(mv->pCorrelationMasks);}
    for(uint32_t i=0;i<info->attachmentCount;i++){
        const VkAttachmentDescription *x=info->pAttachments+i;uint64_t *v=at+i*11;
        v[0]=x->format;v[1]=x->samples;v[2]=x->loadOp;v[3]=x->storeOp;v[4]=x->stencilLoadOp;v[5]=x->stencilStoreOp;v[6]=x->initialLayout;v[7]=x->finalLayout;v[8]=x->flags;v[9]=v[10]=UINT32_MAX;
    }
    for(uint32_t i=0;i<info->subpassCount;i++){
        const VkSubpassDescription *s=info->pSubpasses+i;uint64_t *v=sub+i*14;
        v[0]=s->flags;v[1]=s->pipelineBindPoint;v[2]=mv && mv->subpassCount?mv->pViewMasks[i]:0;
        v[3]=s->colorAttachmentCount;v[4]=PTR(rg_refs1(&a,s->colorAttachmentCount,s->pColorAttachments));
        v[5]=s->pResolveAttachments?PTR(rg_refs1(&a,s->colorAttachmentCount,s->pResolveAttachments)):0;
        v[6]=s->inputAttachmentCount;v[7]=PTR(rg_refs1(&a,s->inputAttachmentCount,s->pInputAttachments));
        v[8]=s->pDepthStencilAttachment?PTR(rg_refs1(&a,1,s->pDepthStencilAttachment)):0;
        v[9]=s->preserveAttachmentCount;v[10]=PTR(s->pPreserveAttachments);
    }
    for(uint32_t i=0;i<info->dependencyCount;i++){
        const VkSubpassDependency *x=info->pDependencies+i;uint64_t *v=dep+i*8;
        v[0]=x->srcSubpass;v[1]=x->dstSubpass;v[2]=x->srcStageMask;v[3]=x->dstStageMask;v[4]=x->srcAccessMask;v[5]=x->dstAccessMask;v[6]=x->dependencyFlags;v[7]=mv && mv->dependencyCount?(uint64_t)(int64_t)mv->pViewOffsets[i]:0;
    }
    if(aspects && a.result==VK_SUCCESS)for(uint32_t i=0;i<aspects->aspectReferenceCount;i++){
        const VkInputAttachmentAspectReference *r=aspects->pAspectReferences+i;
        if(r->subpass>=info->subpassCount || r->inputAttachmentIndex>=sub[r->subpass*14+6]){a.result=VK_ERROR_FEATURE_NOT_PRESENT;break;}
        ((uint64_t *)(uintptr_t)sub[r->subpass*14+7])[r->inputAttachmentIndex*4+2]=r->aspectMask;
    }
    return rg_finish(&a,d,h,info->flags,out);
}
static VkResult render_graph_modern(VkDevice d,const VkRenderPassCreateInfo2 *info,VkRenderPass *out){
    RGArena a={0};uint64_t h[8]={info->attachmentCount,0,info->subpassCount,0,info->dependencyCount,0,info->correlatedViewMaskCount,PTR(info->pCorrelatedViewMasks)};
    uint64_t *at=rg_alloc(&a,info->attachmentCount,11),*sub=rg_alloc(&a,info->subpassCount,14),*dep=rg_alloc(&a,info->dependencyCount,8);
    h[1]=PTR(at);h[3]=PTR(sub);h[5]=PTR(dep);if(a.result!=VK_SUCCESS)return rg_finish(&a,d,h,info->flags,out);
    for(uint32_t i=0;i<info->attachmentCount;i++){
        const VkAttachmentDescription2 *x=info->pAttachments+i;uint64_t *v=at+i*11;
        if(x->sType!=VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2){a.result=VK_ERROR_FEATURE_NOT_PRESENT;break;}
        v[0]=x->format;v[1]=x->samples;v[2]=x->loadOp;v[3]=x->storeOp;v[4]=x->stencilLoadOp;v[5]=x->stencilStoreOp;v[6]=x->initialLayout;v[7]=x->finalLayout;v[8]=x->flags;v[9]=v[10]=UINT32_MAX;
        const VkAttachmentDescriptionStencilLayout *st=x->pNext;
        if(st){if(!VALID(st,VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_STENCIL_LAYOUT)){a.result=VK_ERROR_FEATURE_NOT_PRESENT;break;}v[9]=st->stencilInitialLayout;v[10]=st->stencilFinalLayout;}
    }
    for(uint32_t i=0;i<info->subpassCount;i++){
        const VkSubpassDescription2 *s=info->pSubpasses+i;uint64_t *v=sub+i*14;
        if(s->sType!=VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2){a.result=VK_ERROR_FEATURE_NOT_PRESENT;break;}
        v[0]=s->flags;v[1]=s->pipelineBindPoint;v[2]=s->viewMask;
        v[3]=s->colorAttachmentCount;v[4]=PTR(rg_refs2(&a,s->colorAttachmentCount,s->pColorAttachments));
        v[5]=s->pResolveAttachments?PTR(rg_refs2(&a,s->colorAttachmentCount,s->pResolveAttachments)):0;
        v[6]=s->inputAttachmentCount;v[7]=PTR(rg_refs2(&a,s->inputAttachmentCount,s->pInputAttachments));
        v[8]=s->pDepthStencilAttachment?PTR(rg_refs2(&a,1,s->pDepthStencilAttachment)):0;
        v[9]=s->preserveAttachmentCount;v[10]=PTR(s->pPreserveAttachments);
        const VkSubpassDescriptionDepthStencilResolve *resolve=s->pNext;
        if(resolve){
            if(!VALID(resolve,VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE)){a.result=VK_ERROR_FEATURE_NOT_PRESENT;break;}
            v[11]=resolve->depthResolveMode;v[12]=resolve->stencilResolveMode;
            v[13]=resolve->pDepthStencilResolveAttachment?PTR(rg_refs2(&a,1,resolve->pDepthStencilResolveAttachment)):0;
        }
    }
    for(uint32_t i=0;i<info->dependencyCount;i++){
        const VkSubpassDependency2 *x=info->pDependencies+i;uint64_t *v=dep+i*8;
        if(!VALID(x,VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2)){a.result=VK_ERROR_FEATURE_NOT_PRESENT;break;}
        v[0]=x->srcSubpass;v[1]=x->dstSubpass;v[2]=x->srcStageMask;v[3]=x->dstStageMask;v[4]=x->srcAccessMask;v[5]=x->dstAccessMask;v[6]=x->dependencyFlags;v[7]=(int64_t)x->viewOffset;
    }
    return rg_finish(&a,d,h,info->flags,out);
}

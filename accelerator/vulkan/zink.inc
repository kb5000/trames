// SPDX-License-Identifier: GPL-3.0-or-later
// Driver-backed maintenance1 and timeline operations. Waits poll the original
// driver then yield through the existing Linux scheduler, keeping workers free.
VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(VkDevice d,VkCommandPool pool,VkCommandPoolTrimFlags flags) {checked(CALL(116,KEY(d),(uint64_t)pool,flags));}
VKAPI_ATTR void VKAPI_CALL vkTrimCommandPoolKHR(VkDevice d,VkCommandPool pool,VkCommandPoolTrimFlags flags) {vkTrimCommandPool(d,pool,flags);}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(VkDevice d,VkSemaphore semaphore,uint64_t *out) {return CALL(117,KEY(d),(uint64_t)semaphore,PTR(out));}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValueKHR(VkDevice d,VkSemaphore semaphore,uint64_t *out) {return vkGetSemaphoreCounterValue(d,semaphore,out);}
VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(VkDevice d,const VkSemaphoreSignalInfo *info) {
    CHECK(info,VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO);return CALL(118,KEY(d),(uint64_t)info->semaphore,info->value);
}
VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphoreKHR(VkDevice d,const VkSemaphoreSignalInfo *info) {return vkSignalSemaphore(d,info);}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(VkDevice d,const VkSemaphoreWaitInfo *info,uint64_t timeout) {
    CHECK(info,VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    if(!info->semaphoreCount || info->semaphoreCount>64 || (info->flags&~VK_SEMAPHORE_WAIT_ANY_BIT))return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t began=now_ns();
    for(;;){
        VkResult r=CALL(119,KEY(d),PTR(info->pSemaphores),PTR(info->pValues),info->semaphoreCount,info->flags);
        if(r!=VK_TIMEOUT)return r;
        uint64_t elapsed=now_ns()-began;if(timeout!=UINT64_MAX && elapsed>=timeout)return VK_TIMEOUT;
        struct timespec sleep={0,(long)(timeout==UINT64_MAX || timeout-elapsed>1000000?1000000:timeout-elapsed)};nanosleep(&sleep,NULL);
    }
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphoresKHR(VkDevice d,const VkSemaphoreWaitInfo *info,uint64_t timeout) {return vkWaitSemaphores(d,info,timeout);}

// Renderpass2 uses the original KHR driver entrypoints; profile limits match the
// legacy single-subpass path. Every nested record is flattened before dispatch.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice d,const VkRenderPassCreateInfo2 *info,const VkAllocationCallbacks *allocator,VkRenderPass *out) {
    CHECK(info,VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2);ALLOCATOR(allocator);
    if(!(D(d)->enabled&128) || info->flags || (info->attachmentCount!=1 && info->attachmentCount!=2) || info->subpassCount!=1 || info->dependencyCount>8 || info->correlatedViewMaskCount)return VK_ERROR_FEATURE_NOT_PRESENT;
    VkAttachmentDescription attachments[2];
    for(uint32_t i=0;i<info->attachmentCount;i++){
        const VkAttachmentDescription2 *a=info->pAttachments+i;CHECK(a,VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2);
        attachments[i]=(VkAttachmentDescription){a->flags,a->format,a->samples,a->loadOp,a->storeOp,a->stencilLoadOp,a->stencilStoreOp,a->initialLayout,a->finalLayout};
    }
    const VkSubpassDescription2 *s=info->pSubpasses;CHECK(s,VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2);
    if(s->flags || s->viewMask || s->pipelineBindPoint!=VK_PIPELINE_BIND_POINT_GRAPHICS || s->inputAttachmentCount || s->colorAttachmentCount!=1 || s->pResolveAttachments || s->preserveAttachmentCount)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkAttachmentReference2 *ref=s->pColorAttachments;CHECK(ref,VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2);
    if(ref->attachment!=0 || ref->layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || (ref->aspectMask && ref->aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT))return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t depth[5];
    if(info->attachmentCount==2){
        ref=s->pDepthStencilAttachment;CHECK(ref,VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2);
        const VkAttachmentDescription *z=attachments+1;
        if(ref->attachment!=1 || ref->layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || (ref->aspectMask && ref->aspectMask!=VK_IMAGE_ASPECT_DEPTH_BIT) || z->flags || z->samples!=VK_SAMPLE_COUNT_1_BIT || (z->format!=VK_FORMAT_D16_UNORM && z->format!=VK_FORMAT_D32_SFLOAT))return VK_ERROR_FEATURE_NOT_PRESENT;
        depth[0]=z->format;depth[1]=z->loadOp;depth[2]=z->storeOp;depth[3]=z->initialLayout;depth[4]=z->finalLayout;
    }else if(s->pDepthStencilAttachment && s->pDepthStencilAttachment->attachment!=VK_ATTACHMENT_UNUSED)return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkAttachmentDescription *a=attachments;if(a->flags || a->samples!=VK_SAMPLE_COUNT_1_BIT)return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t dependencies[8][7];for(uint32_t i=0;i<info->dependencyCount;i++){
        const VkSubpassDependency2 *x=info->pDependencies+i;CHECK(x,VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2);if(x->viewOffset)return VK_ERROR_FEATURE_NOT_PRESENT;
        dependencies[i][0]=x->srcSubpass;dependencies[i][1]=x->dstSubpass;dependencies[i][2]=x->srcStageMask;dependencies[i][3]=x->dstStageMask;dependencies[i][4]=x->srcAccessMask;dependencies[i][5]=x->dstAccessMask;dependencies[i][6]=x->dependencyFlags;
    }
    return CALL(122,KEY(d),a->format,a->loadOp,a->storeOp,a->initialLayout,a->finalLayout,PTR(dependencies),info->dependencyCount,PTR(out),info->attachmentCount==2?PTR(depth):0);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2KHR(VkDevice d,const VkRenderPassCreateInfo2 *info,const VkAllocationCallbacks *allocator,VkRenderPass *out) {return vkCreateRenderPass2(d,info,allocator,out);}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(VkCommandBuffer c,const VkRenderPassBeginInfo *info,const VkSubpassBeginInfo *begin) {if(!VALID(begin,VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO))checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(begin_pass(c,info,begin->contents,123));}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2KHR(VkCommandBuffer c,const VkRenderPassBeginInfo *info,const VkSubpassBeginInfo *begin) {vkCmdBeginRenderPass2(c,info,begin);}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(VkCommandBuffer c,const VkSubpassEndInfo *end) {if(!VALID(end,VK_STRUCTURE_TYPE_SUBPASS_END_INFO))checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(124,DEVKEY(c),KEY(c)));}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2KHR(VkCommandBuffer c,const VkSubpassEndInfo *end) {vkCmdEndRenderPass2(c,end);}
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(VkCommandBuffer c,const VkSubpassBeginInfo *begin,const VkSubpassEndInfo *end) {
    // No valid transition exists in a renderpass with exactly one subpass.
    (void)c;(void)begin;(void)end;checked(VK_ERROR_FEATURE_NOT_PRESENT);
}
VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2KHR(VkCommandBuffer c,const VkSubpassBeginInfo *begin,const VkSubpassEndInfo *end) {vkCmdNextSubpass2(c,begin,end);}

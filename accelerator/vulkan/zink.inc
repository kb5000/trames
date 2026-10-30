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
    if(D(d)->bridge&2)return CALL(202,KEY(d),PTR(info->pSemaphores),PTR(info->pValues),info->semaphoreCount,info->flags,timeout);
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
// legacy single-subpass/MRT path. Every nested record is flattened before dispatch.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(VkDevice d,const VkRenderPassCreateInfo2 *info,const VkAllocationCallbacks *allocator,VkRenderPass *out) {
    if(!VALID(info,VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2))return unsupported_render_pass(__FILE__,__LINE__);ALLOCATOR(allocator);
    if(!(D(d)->enabled&128) || info->attachmentCount>17 || info->subpassCount!=1 || info->dependencyCount>8 || info->correlatedViewMaskCount)return unsupported_render_pass(__FILE__,__LINE__);
    VkAttachmentDescription attachments[17];
    for(uint32_t i=0;i<info->attachmentCount;i++){
        const VkAttachmentDescription2 *a=info->pAttachments+i;if(!VALID(a,VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2))return unsupported_render_pass(__FILE__,__LINE__);
        attachments[i]=(VkAttachmentDescription){a->flags,a->format,a->samples,a->loadOp,a->storeOp,a->stencilLoadOp,a->stencilStoreOp,a->initialLayout,a->finalLayout};
    }
    const VkSubpassDescription2 *s=info->pSubpasses;if(!VALID(s,VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2))return unsupported_render_pass(__FILE__,__LINE__);
    if(s->viewMask || s->colorAttachmentCount>8)return unsupported_render_pass(__FILE__,__LINE__);
    VkAttachmentReference colors[8],resolves[8],depth={VK_ATTACHMENT_UNUSED,VK_IMAGE_LAYOUT_UNDEFINED};
    for(unsigned i=0;i<s->colorAttachmentCount;i++){
        const VkAttachmentReference2 *r=s->pColorAttachments+i;if(!VALID(r,VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2))return unsupported_render_pass(__FILE__,__LINE__);
        // VkAttachmentReference2::aspectMask applies only to input attachments.
        // Color/depth-stencil output references ignore it (including UNUSED).
        colors[i]=(VkAttachmentReference){r->attachment,r->layout};
    }
    if(s->pResolveAttachments)for(unsigned i=0;i<s->colorAttachmentCount;i++){
        const VkAttachmentReference2 *r=s->pResolveAttachments+i;if(!VALID(r,VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2))return unsupported_render_pass(__FILE__,__LINE__);
        resolves[i]=(VkAttachmentReference){r->attachment,r->layout};
    }
    if(s->pDepthStencilAttachment){
        const VkAttachmentReference2 *r=s->pDepthStencilAttachment;if(!VALID(r,VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2))return unsupported_render_pass(__FILE__,__LINE__);
        depth=(VkAttachmentReference){r->attachment,r->layout};
    }
    VkSubpassDependency dependencies[8];for(uint32_t i=0;i<info->dependencyCount;i++){
        const VkSubpassDependency2 *x=info->pDependencies+i;if(!VALID(x,VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2))return unsupported_render_pass(__FILE__,__LINE__);if(x->viewOffset)return unsupported_render_pass(__FILE__,__LINE__);
        dependencies[i]=(VkSubpassDependency){x->srcSubpass,x->dstSubpass,x->srcStageMask,x->dstStageMask,x->srcAccessMask,x->dstAccessMask,x->dependencyFlags};
    }
    // Unsupported nested attachments are rejected before conversion can discard
    // their pointers or metadata. All other validation is shared with legacy.
    if(s->inputAttachmentCount || s->preserveAttachmentCount)return unsupported_render_pass(__FILE__,__LINE__);
    VkSubpassDescription subpass={.flags=s->flags,.pipelineBindPoint=s->pipelineBindPoint,.colorAttachmentCount=s->colorAttachmentCount,.pColorAttachments=colors,.pResolveAttachments=s->pResolveAttachments?resolves:NULL,.pDepthStencilAttachment=&depth};
    VkRenderPassCreateInfo legacy={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.flags=info->flags,.attachmentCount=info->attachmentCount,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=info->dependencyCount,.pDependencies=dependencies};
    return render_pass(d,&legacy,out,122);
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

// Query data and transform-feedback counters remain native GPU objects. WAIT
// yields through Linux instead of holding a native worker in a driver wait.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice d,const VkQueryPoolCreateInfo *info,const VkAllocationCallbacks *allocator,VkQueryPool *out) {
    CHECK(info,VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);ALLOCATOR(allocator);
    if(info->flags || info->pipelineStatistics)return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(138,KEY(d),info->queryType,info->queryCount,PTR(out));
}
VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice d,VkQueryPool pool,const VkAllocationCallbacks *allocator) {
    if(pool)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(139,KEY(d),(uint64_t)pool));
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice d,VkQueryPool pool,uint32_t first,uint32_t count,size_t size,void *out,VkDeviceSize stride,VkQueryResultFlags flags) {
    for(;;){
        VkResult r=CALL(141,KEY(d),(uint64_t)pool,first,count,size,PTR(out),stride,flags);
        if(r!=VK_NOT_READY || !(flags&VK_QUERY_RESULT_WAIT_BIT))return r;
        struct timespec sleep={0,1000000};nanosleep(&sleep,NULL);
    }
}
VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer c,VkQueryPool pool,uint32_t first,uint32_t count) {checked(CALL(140,DEVKEY(c),KEY(c),(uint64_t)pool,first,count));}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(VkCommandBuffer c,VkQueryPool pool,uint32_t first,uint32_t count,VkBuffer dst,VkDeviceSize offset,VkDeviceSize stride,VkQueryResultFlags flags) {
    checked(CALL(142,DEVKEY(c),KEY(c),(uint64_t)pool,first,count,(uint64_t)dst,offset,stride,flags));
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer c,VkPipelineStageFlagBits stage,VkQueryPool pool,uint32_t query) {checked(CALL(143,DEVKEY(c),KEY(c),(uint64_t)pool,query,stage));}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer c,VkQueryPool pool,uint32_t query,VkQueryControlFlags flags) {checked(CALL(144,DEVKEY(c),KEY(c),(uint64_t)pool,query,flags));}
VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer c,VkQueryPool pool,uint32_t query) {checked(CALL(145,DEVKEY(c),KEY(c),(uint64_t)pool,query));}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginQueryIndexedEXT(VkCommandBuffer c,VkQueryPool pool,uint32_t query,VkQueryControlFlags flags,uint32_t index) {checked(CALL(144,DEVKEY(c),KEY(c),(uint64_t)pool,query,flags,index,1));}
VKAPI_ATTR void VKAPI_CALL vkCmdEndQueryIndexedEXT(VkCommandBuffer c,VkQueryPool pool,uint32_t query,uint32_t index) {checked(CALL(145,DEVKEY(c),KEY(c),(uint64_t)pool,query,0,index,1));}
VKAPI_ATTR void VKAPI_CALL vkCmdBindTransformFeedbackBuffersEXT(VkCommandBuffer c,uint32_t first,uint32_t count,const VkBuffer *buffers,const VkDeviceSize *offsets,const VkDeviceSize *sizes) {
    if(count>4)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    checked(CALL(146,DEVKEY(c),KEY(c),first,count,PTR(buffers),PTR(offsets),PTR(sizes)));
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginTransformFeedbackEXT(VkCommandBuffer c,uint32_t first,uint32_t count,const VkBuffer *counters,const VkDeviceSize *offsets) {
    if(count>4)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    checked(CALL(147,DEVKEY(c),KEY(c),first,count,PTR(counters),PTR(offsets)));
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndTransformFeedbackEXT(VkCommandBuffer c,uint32_t first,uint32_t count,const VkBuffer *counters,const VkDeviceSize *offsets) {
    if(count>4)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    checked(CALL(148,DEVKEY(c),KEY(c),first,count,PTR(counters),PTR(offsets)));
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectByteCountEXT(VkCommandBuffer c,uint32_t instances,uint32_t firstInstance,VkBuffer counter,VkDeviceSize offset,uint32_t subtract,uint32_t stride) {
    checked(CALL(149,DEVKEY(c),KEY(c),instances,firstInstance,(uint64_t)counter,offset,subtract,stride));
}

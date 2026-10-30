// SPDX-License-Identifier: GPL-3.0-or-later
// Core events and layout/accounting queries use original native objects.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(VkDevice d,const VkEventCreateInfo *info,const VkAllocationCallbacks *allocator,VkEvent *out){CHECK(info,VK_STRUCTURE_TYPE_EVENT_CREATE_INFO);ALLOCATOR(allocator);return CALL(220,KEY(d),info->flags,PTR(out));}
VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(VkDevice d,VkEvent event,const VkAllocationCallbacks *allocator){if(event)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(221,KEY(d),(uint64_t)event));}
VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice d,VkEvent event){return CALL(222,KEY(d),(uint64_t)event);}
VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice d,VkEvent event){return CALL(223,KEY(d),(uint64_t)event);}
VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice d,VkEvent event){return CALL(224,KEY(d),(uint64_t)event);}
VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(VkCommandBuffer c,VkEvent event,VkPipelineStageFlags stage){checked(CALL(225,DEVKEY(c),KEY(c),(uint64_t)event,stage));}
VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(VkCommandBuffer c,VkEvent event,VkPipelineStageFlags stage){checked(CALL(226,DEVKEY(c),KEY(c),(uint64_t)event,stage));}
VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(VkCommandBuffer c,uint32_t count,const VkEvent *events,VkPipelineStageFlags src,VkPipelineStageFlags dst,uint32_t nm,const VkMemoryBarrier *memory,uint32_t nb,const VkBufferMemoryBarrier *buffers,uint32_t ni,const VkImageMemoryBarrier *images){
    uint64_t *b=nb?malloc((size_t)nb*7*8):NULL,*im=ni?malloc((size_t)ni*10*8):NULL,*mem=nm?malloc((size_t)nm*2*8):NULL;
    if((nb&&!b)||(ni&&!im)||(nm&&!mem)){free(b);free(im);free(mem);checked(VK_ERROR_OUT_OF_HOST_MEMORY);}
    for(uint32_t i=0;i<nm;i++){if(!VALID(memory+i,VK_STRUCTURE_TYPE_MEMORY_BARRIER))checked(VK_ERROR_FEATURE_NOT_PRESENT);mem[i*2]=memory[i].srcAccessMask;mem[i*2+1]=memory[i].dstAccessMask;}
    for(uint32_t i=0;i<nb;i++){const VkBufferMemoryBarrier *v=buffers+i;if(!VALID(v,VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER))checked(VK_ERROR_FEATURE_NOT_PRESENT);uint64_t *x=b+i*7;x[0]=(uint64_t)v->buffer;x[1]=v->srcAccessMask;x[2]=v->dstAccessMask;x[3]=v->srcQueueFamilyIndex;x[4]=v->dstQueueFamilyIndex;x[5]=v->offset;x[6]=v->size;}
    for(uint32_t i=0;i<ni;i++){const VkImageMemoryBarrier *v=images+i;if(!VALID(v,VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER))checked(VK_ERROR_FEATURE_NOT_PRESENT);uint64_t *x=im+i*10;x[0]=(uint64_t)v->image;x[1]=v->srcAccessMask;x[2]=v->dstAccessMask;x[3]=v->oldLayout;x[4]=v->newLayout;x[5]=v->subresourceRange.aspectMask;x[6]=v->subresourceRange.baseMipLevel;x[7]=v->subresourceRange.levelCount;x[8]=v->subresourceRange.baseArrayLayer;x[9]=v->subresourceRange.layerCount;}
    uint64_t h[10]={count,PTR(events),src,dst,nm,PTR(mem),nb,PTR(b),ni,PTR(im)};VkResult r=CALL(227,DEVKEY(c),KEY(c),PTR(h));free(b);free(im);free(mem);checked(r);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(VkDevice d,VkDeviceMemory memory,VkDeviceSize *out){checked(CALL(228,KEY(d),(uint64_t)memory,PTR(out)));}
VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(VkDevice d,VkImage image,const VkImageSubresource *sub,VkSubresourceLayout *out){checked(CALL(229,KEY(d),(uint64_t)image,sub->aspectMask,sub->mipLevel,sub->arrayLayer,PTR(out)));}

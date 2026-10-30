// SPDX-License-Identifier: GPL-3.0-or-later
// Remaining promoted commands use owned wire records and original GPU calls.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversion(VkDevice d,const VkSamplerYcbcrConversionCreateInfo *info,const VkAllocationCallbacks *allocator,VkSamplerYcbcrConversion *out){
    CHECK(info,VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO);ALLOCATOR(allocator);
    uint64_t w[11]={info->format,info->ycbcrModel,info->ycbcrRange,info->components.r,info->components.g,info->components.b,info->components.a,info->xChromaOffset,info->yChromaOffset,info->chromaFilter,info->forceExplicitReconstruction};
    return CALL(230,KEY(d),PTR(w),PTR(out));
}
VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversion(VkDevice d,VkSamplerYcbcrConversion c,const VkAllocationCallbacks *allocator){if(c)checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(231,KEY(d),(uint64_t)c));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversionKHR(VkDevice d,const VkSamplerYcbcrConversionCreateInfo *info,const VkAllocationCallbacks *a,VkSamplerYcbcrConversion *out){return vkCreateSamplerYcbcrConversion(d,info,a,out);}
VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversionKHR(VkDevice d,VkSamplerYcbcrConversion c,const VkAllocationCallbacks *a){vkDestroySamplerYcbcrConversion(d,c,a);}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeatures(VkDevice d,uint32_t heap,uint32_t local,uint32_t remote,VkPeerMemoryFeatureFlags *out){checked(CALL(232,KEY(d),heap,local,remote,PTR(out)));}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeaturesKHR(VkDevice d,uint32_t h,uint32_t l,uint32_t r,VkPeerMemoryFeatureFlags *out){vkGetDeviceGroupPeerMemoryFeatures(d,h,l,r,out);}
VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(VkDevice d,VkImage image,uint32_t *count,VkSparseImageMemoryRequirements *out){(void)out;checked(CALL(233,KEY(d),(uint64_t)image,PTR(count)));}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(VkQueue q,uint32_t count,const VkBindSparseInfo *infos,VkFence fence){
    // No sparse binding feature/queue bit is exposed. Empty batches still have
    // ordinary queue/semaphore/fence semantics rather than fabricated success.
    VkSubmitInfo *submits=calloc(count?count:1,sizeof(*submits));if(!submits)return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t *stages=calloc(1,sizeof(*stages));if(!stages){free(submits);return VK_ERROR_OUT_OF_HOST_MEMORY;}
    uint32_t maximum=0;
    for(uint32_t i=0;i<count;i++){
        if(!VALID(infos+i,VK_STRUCTURE_TYPE_BIND_SPARSE_INFO)||infos[i].bufferBindCount||infos[i].imageOpaqueBindCount||infos[i].imageBindCount){free(stages);free(submits);return VK_ERROR_FEATURE_NOT_PRESENT;}
        if(infos[i].waitSemaphoreCount>maximum)maximum=infos[i].waitSemaphoreCount;
    }
    if(maximum){free(stages);stages=calloc(maximum,sizeof(*stages));if(!stages){free(submits);return VK_ERROR_OUT_OF_HOST_MEMORY;}for(uint32_t i=0;i<maximum;i++)stages[i]=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;}
    for(uint32_t i=0;i<count;i++)submits[i]=(VkSubmitInfo){.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=infos[i].waitSemaphoreCount,.pWaitSemaphores=infos[i].pWaitSemaphores,.pWaitDstStageMask=stages,.signalSemaphoreCount=infos[i].signalSemaphoreCount,.pSignalSemaphores=infos[i].pSignalSemaphores};
    VkResult result=vkQueueSubmit(q,count,submits,fence);free(stages);free(submits);return result;
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,VkBuffer count,VkDeviceSize countOffset,uint32_t maxDrawCount,uint32_t stride){checked(CALL(235,DEVKEY(c),KEY(c),(uint64_t)buffer,offset,maxDrawCount,stride,(uint64_t)count,countOffset));}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount(VkCommandBuffer c,VkBuffer buffer,VkDeviceSize offset,VkBuffer count,VkDeviceSize countOffset,uint32_t maxDrawCount,uint32_t stride){checked(CALL(236,DEVKEY(c),KEY(c),(uint64_t)buffer,offset,maxDrawCount,stride,(uint64_t)count,countOffset));}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCountKHR(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,VkBuffer n,VkDeviceSize no,uint32_t max,uint32_t stride){vkCmdDrawIndirectCount(c,b,o,n,no,max,stride);}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCountKHR(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,VkBuffer n,VkDeviceSize no,uint32_t max,uint32_t stride){vkCmdDrawIndexedIndirectCount(c,b,o,n,no,max,stride);}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddress(VkDevice d,const VkBufferDeviceAddressInfo *info){uint64_t out=0;if(!VALID(info,VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO))checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(237,KEY(d),(uint64_t)info->buffer,PTR(&out)));return out;}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddress(VkDevice d,const VkDeviceMemoryOpaqueCaptureAddressInfo *info){uint64_t out=0;if(!VALID(info,VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO))checked(VK_ERROR_FEATURE_NOT_PRESENT);checked(CALL(238,KEY(d),(uint64_t)info->memory,PTR(&out)));return out;}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddressKHR(VkDevice d,const VkBufferDeviceAddressInfo *i){return vkGetBufferOpaqueCaptureAddress(d,i);}
VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddressKHR(VkDevice d,const VkDeviceMemoryOpaqueCaptureAddressInfo *i){return vkGetDeviceMemoryOpaqueCaptureAddress(d,i);}

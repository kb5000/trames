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

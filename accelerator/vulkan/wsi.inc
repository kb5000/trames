// SPDX-License-Identifier: GPL-3.0-or-later
// One fixed display/plane/mode, backed solely by the host-authorized Metal layer.
#define ONE(count,out,value) do {if(!(out)){*(count)=1;return VK_SUCCESS;}if(!*(count))return VK_INCOMPLETE;*(count)=1;*(out)=(value);return VK_SUCCESS;}while(0)
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice p,uint32_t *count,VkDisplayPropertiesKHR *out) {
    uint64_t info[3]={0};if(!display_info(KEY(p),info)){*count=0;return VK_SUCCESS;}
    VkDisplayPropertiesKHR value={.display=(VkDisplayKHR)1,.displayName="Display",.physicalDimensions={320,240},.physicalResolution={(uint32_t)info[1],(uint32_t)info[2]},.supportedTransforms=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,.persistentContent=VK_TRUE};ONE(count,out,value);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice p,uint32_t *count,VkDisplayPlanePropertiesKHR *out) {
    uint64_t info[3]={0};if(!display_info(KEY(p),info)){*count=0;return VK_SUCCESS;}VkDisplayPlanePropertiesKHR value={.currentDisplay=(VkDisplayKHR)1,.currentStackIndex=0};ONE(count,out,value);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice p,uint32_t plane,uint32_t *count,VkDisplayKHR *out) {
    uint64_t info[3]={0};if(plane || !display_info(KEY(p),info))return VK_ERROR_INITIALIZATION_FAILED;VkDisplayKHR value=(VkDisplayKHR)1;ONE(count,out,value);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModePropertiesKHR(VkPhysicalDevice p,VkDisplayKHR display,uint32_t *count,VkDisplayModePropertiesKHR *out) {
    uint64_t info[3]={0};if((uint64_t)display!=1 || !display_info(KEY(p),info))return VK_ERROR_INITIALIZATION_FAILED;
    VkDisplayModePropertiesKHR value={.displayMode=(VkDisplayModeKHR)1,.parameters={{(uint32_t)info[1],(uint32_t)info[2]},60000}};ONE(count,out,value);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayModeKHR(VkPhysicalDevice p,VkDisplayKHR display,const VkDisplayModeCreateInfoKHR *info,const VkAllocationCallbacks *allocator,VkDisplayModeKHR *out) {
    CHECK(info,VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR);ALLOCATOR(allocator);uint64_t d[3]={0};
    if((uint64_t)display!=1 || info->flags || !display_info(KEY(p),d) || info->parameters.visibleRegion.width!=d[1] || info->parameters.visibleRegion.height!=d[2] || info->parameters.refreshRate!=60000)return VK_ERROR_FEATURE_NOT_PRESENT;
    *out=(VkDisplayModeKHR)1;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice p,VkDisplayModeKHR mode,uint32_t plane,VkDisplayPlaneCapabilitiesKHR *out) {
    uint64_t info[3]={0};if((uint64_t)mode!=1 || plane || !display_info(KEY(p),info))return VK_ERROR_INITIALIZATION_FAILED;
    memset(out,0,sizeof(*out));out->supportedAlpha=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    out->minSrcExtent=out->maxSrcExtent=out->minDstExtent=out->maxDstExtent=(VkExtent2D){(uint32_t)info[1],(uint32_t)info[2]};return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayPlaneSurfaceKHR(VkInstance i,const VkDisplaySurfaceCreateInfoKHR *info,const VkAllocationCallbacks *allocator,VkSurfaceKHR *out) {
    CHECK(info,VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR);ALLOCATOR(allocator);uint64_t d[3]={0};
    if(info->flags || (uint64_t)info->displayMode!=1 || info->planeIndex || info->planeStackIndex || info->transform!=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR || info->alphaMode!=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR || info->globalAlpha!=1.0f || !display_info(0,d) || info->imageExtent.width!=d[1] || info->imageExtent.height!=d[2])return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(81,KEY(i),PTR(out));
}
VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance i,VkSurfaceKHR surface,const VkAllocationCallbacks *allocator) {if(surface){checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(82,KEY(i),(uint64_t)surface));wayland_forget_surface(surface);}}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice p,uint32_t family,VkSurfaceKHR surface,VkBool32 *out) {return CALL(83,KEY(p),(uint64_t)surface,family,PTR(out));}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice p,VkSurfaceKHR surface,VkSurfaceCapabilitiesKHR *out) {return CALL(84,KEY(p),(uint64_t)surface,PTR(out));}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice p,VkSurfaceKHR surface,uint32_t *count,VkSurfaceFormatKHR *out) {return CALL(85,KEY(p),(uint64_t)surface,PTR(count),PTR(out));}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice p,VkSurfaceKHR surface,uint32_t *count,VkPresentModeKHR *out) {return CALL(86,KEY(p),(uint64_t)surface,PTR(count),PTR(out));}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice d,const VkSwapchainCreateInfoKHR *info,const VkAllocationCallbacks *allocator,VkSwapchainKHR *out) {
    CHECK(info,VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);ALLOCATOR(allocator);
    if(info->flags || info->imageArrayLayers!=1 || info->imageSharingMode!=VK_SHARING_MODE_EXCLUSIVE || info->preTransform!=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR || info->compositeAlpha!=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR || (info->presentMode!=VK_PRESENT_MODE_FIFO_KHR && info->presentMode!=VK_PRESENT_MODE_MAILBOX_KHR) || !info->clipped)return VK_ERROR_FEATURE_NOT_PRESENT;
    VkResult r=CALL(87,KEY(d),(uint64_t)info->surface,info->minImageCount,info->imageFormat,info->imageColorSpace,info->imageExtent.width,info->imageExtent.height,info->imageUsage,(uint64_t)info->oldSwapchain,info->presentMode==VK_PRESENT_MODE_MAILBOX_KHR,PTR(out));
    if(r==VK_SUCCESS && !wayland_register_chain(*out,info->surface)){CALL(88,KEY(d),(uint64_t)*out);return VK_ERROR_OUT_OF_HOST_MEMORY;}return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice d,VkSwapchainKHR chain,const VkAllocationCallbacks *allocator) {if(chain){checked(allocator?VK_ERROR_FEATURE_NOT_PRESENT:CALL(88,KEY(d),(uint64_t)chain));wayland_forget_chain(chain);}}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice d,VkSwapchainKHR chain,uint32_t *count,VkImage *out) {return CALL(89,KEY(d),(uint64_t)chain,PTR(count),PTR(out));}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice d,VkSwapchainKHR chain,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t *out) {
    uint64_t began=now_ns();for(;;){VkResult r=CALL(90,KEY(d),(uint64_t)chain,(uint64_t)semaphore,(uint64_t)fence,PTR(out));if(r!=VK_NOT_READY && r!=VK_TIMEOUT)return r;
        if(!timeout)return VK_NOT_READY;uint64_t elapsed=now_ns()-began;if(timeout!=UINT64_MAX && elapsed>=timeout)return VK_TIMEOUT;
        struct timespec sleep={0,(long)(timeout==UINT64_MAX || timeout-elapsed>1000000?1000000:timeout-elapsed)};nanosleep(&sleep,NULL);
    }
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue q,const VkPresentInfoKHR *info) {
    CHECK(info,VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);if(info->swapchainCount!=1 || info->waitSemaphoreCount>64)return VK_ERROR_FEATURE_NOT_PRESENT;
    VkResult r;
    do {r=CALL(91,DEVKEY(q),KEY(q),(uint64_t)info->pSwapchains[0],info->pImageIndices[0],PTR(info->pWaitSemaphores),info->waitSemaphoreCount);
        if(r==VK_NOT_READY){struct timespec pause={0,1000000};nanosleep(&pause,NULL);}
    } while(r==VK_NOT_READY);
    if(r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR)wayland_present(info->pSwapchains[0]);
    if(info->pResults)info->pResults[0]=r;return r;
}
#undef ONE

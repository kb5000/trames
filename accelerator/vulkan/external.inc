// SPDX-License-Identifier: GPL-3.0-or-later
// Import-only native image-plane profile. Guest FDs never become host FDs.
static int native_external_format(Dispatch *p,VkFormat format) {
    return (p->capabilities[0]&8192) && (format==VK_FORMAT_R8_UNORM || format==VK_FORMAT_R8G8_UNORM || format==VK_FORMAT_R8G8B8A8_UNORM || format==VK_FORMAT_B8G8R8A8_UNORM);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdKHR(VkDevice d,const VkMemoryGetFdInfoKHR *info,int *out) {
    CHECK(info,VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);
    return CALL(240,KEY(d),(uint64_t)info->memory,info->handleType,PTR(out));
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdPropertiesKHR(VkDevice d,VkExternalMemoryHandleTypeFlagBits type,int fd,VkMemoryFdPropertiesKHR *out) {
    if(!out || out->sType!=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR || fd<0)return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    return CALL(241,KEY(d),(uint64_t)fd,type,PTR(&out->memoryTypeBits));
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetImageDrmFormatModifierPropertiesEXT(VkDevice d,VkImage image,VkImageDrmFormatModifierPropertiesEXT *out) {
    if(!out || out->sType!=VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT)return VK_ERROR_FEATURE_NOT_PRESENT;
    return CALL(242,KEY(d),(uint64_t)image,PTR(&out->drmFormatModifier));
}

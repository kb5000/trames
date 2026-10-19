// SPDX-License-Identifier: GPL-3.0-or-later
// Standard Vulkan loader ABI, query-layout/canary and extension-only device tests.
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(call) assert((call)==VK_SUCCESS)
struct unknown {VkStructureType sType;void *pNext;uint64_t canary;};
static int same_requirements(VkMemoryRequirements a,VkMemoryRequirements b) {
    return a.size==b.size && a.alignment==b.alignment && a.memoryTypeBits==b.memoryTypeBits;
}
int main(int argc,char **argv) {
    int bridge=argc>1 && !strcmp(argv[1],"bridge");
    uint32_t version;OK(vkEnumerateInstanceVersion(&version));assert(version>=VK_API_VERSION_1_1);
    const char *extension=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledExtensionCount=1,.ppEnabledExtensionNames=&extension};
    VkInstance instance;OK(vkCreateInstance(&info,NULL,&instance));
    uint32_t n=1;VkPhysicalDevice p;OK(vkEnumeratePhysicalDevices(instance,&n,&p));assert(n==1);
    struct unknown unknown={(VkStructureType)0x7ffffffe,NULL,0xcafe123456789abcULL};
    VkPhysicalDeviceProperties properties={0};vkGetPhysicalDeviceProperties(p,&properties);
    if(bridge)assert(properties.apiVersion==VK_API_VERSION_1_0);
    VkPhysicalDeviceProperties2 properties2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&unknown};
    vkGetPhysicalDeviceProperties2(p,&properties2);
    // Native drivers may leave padding unspecified; compare semantic fields.
    VkPhysicalDeviceProperties *p2=&properties2.properties;
    assert(properties.apiVersion==p2->apiVersion && properties.driverVersion==p2->driverVersion && properties.vendorID==p2->vendorID && properties.deviceID==p2->deviceID && properties.deviceType==p2->deviceType);
    assert(!strcmp(properties.deviceName,p2->deviceName) && !memcmp(properties.pipelineCacheUUID,p2->pipelineCacheUUID,VK_UUID_SIZE));
    assert(properties.limits.minMemoryMapAlignment==p2->limits.minMemoryMapAlignment && properties.limits.nonCoherentAtomSize==p2->limits.nonCoherentAtomSize && properties.limits.maxImageDimension2D==p2->limits.maxImageDimension2D);
    assert(properties2.pNext==&unknown && unknown.canary==0xcafe123456789abcULL);
    PFN_vkGetPhysicalDeviceFeatures2KHR features_khr=(PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceFeatures2KHR");assert(features_khr);
    VkPhysicalDeviceFeatures legacy_features={0};vkGetPhysicalDeviceFeatures(p,&legacy_features);
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&unknown};features_khr(p,&features);
    assert(!memcmp(&features.features,&legacy_features,sizeof(legacy_features)));
    VkPhysicalDeviceMemoryProperties legacy_memory={0};vkGetPhysicalDeviceMemoryProperties(p,&legacy_memory);
    VkPhysicalDeviceMemoryProperties2 memory={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,.pNext=&unknown};vkGetPhysicalDeviceMemoryProperties2(p,&memory);
    assert(memory.memoryProperties.memoryTypeCount==legacy_memory.memoryTypeCount && memory.memoryProperties.memoryHeapCount==legacy_memory.memoryHeapCount);
    for(unsigned i=0;i<legacy_memory.memoryTypeCount;i++)assert(memory.memoryProperties.memoryTypes[i].propertyFlags==legacy_memory.memoryTypes[i].propertyFlags && memory.memoryProperties.memoryTypes[i].heapIndex==legacy_memory.memoryTypes[i].heapIndex);
    for(unsigned i=0;i<legacy_memory.memoryHeapCount;i++)assert(memory.memoryProperties.memoryHeaps[i].size==legacy_memory.memoryHeaps[i].size && memory.memoryProperties.memoryHeaps[i].flags==legacy_memory.memoryHeaps[i].flags);
    VkFormatProperties legacy_format={0};vkGetPhysicalDeviceFormatProperties(p,VK_FORMAT_R8G8B8A8_UNORM,&legacy_format);
    VkFormatProperties2 format={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,.pNext=&unknown};vkGetPhysicalDeviceFormatProperties2(p,VK_FORMAT_R8G8B8A8_UNORM,&format);
    assert(!memcmp(&format.formatProperties,&legacy_format,sizeof(legacy_format)));
    VkPhysicalDeviceImageFormatInfo2 image_info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,.format=VK_FORMAT_R8G8B8A8_UNORM,.type=VK_IMAGE_TYPE_2D,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    VkImageFormatProperties2 image_format={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&unknown};
    VkImageFormatProperties legacy_image={0};OK(vkGetPhysicalDeviceImageFormatProperties(p,image_info.format,image_info.type,image_info.tiling,image_info.usage,0,&legacy_image));OK(vkGetPhysicalDeviceImageFormatProperties2(p,&image_info,&image_format));
    VkImageFormatProperties *i2=&image_format.imageFormatProperties;
    assert(!memcmp(&i2->maxExtent,&legacy_image.maxExtent,sizeof(VkExtent3D)) && i2->maxMipLevels==legacy_image.maxMipLevels && i2->maxArrayLayers==legacy_image.maxArrayLayers && i2->sampleCounts==legacy_image.sampleCounts && i2->maxResourceSize==legacy_image.maxResourceSize);
    VkPhysicalDeviceSparseImageFormatInfo2 sparse_format={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_INFO_2,.format=VK_FORMAT_R8G8B8A8_UNORM,.type=VK_IMAGE_TYPE_2D,.samples=VK_SAMPLE_COUNT_1_BIT,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL};
    uint32_t sparse_count=0,legacy_sparse_count=0;vkGetPhysicalDeviceSparseImageFormatProperties(p,sparse_format.format,sparse_format.type,sparse_format.samples,sparse_format.usage,sparse_format.tiling,&legacy_sparse_count,NULL);vkGetPhysicalDeviceSparseImageFormatProperties2(p,&sparse_format,&sparse_count,NULL);assert(sparse_count==legacy_sparse_count);
    uint32_t count=0;vkGetPhysicalDeviceQueueFamilyProperties2(p,&count,NULL);assert(count>0 && count<64);
    struct queue_guard {VkQueueFamilyProperties2 out[64];uint64_t canary;} queues={.canary=0x1234};
    for(unsigned i=0;i<64;i++)queues.out[i]=(VkQueueFamilyProperties2){.sType=VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2,.pNext=&unknown};
    uint32_t capacity=1;vkGetPhysicalDeviceQueueFamilyProperties2(p,&capacity,queues.out);assert(capacity==1 && queues.canary==0x1234);
    VkQueueFamilyProperties q[64];uint32_t old_count=count;vkGetPhysicalDeviceQueueFamilyProperties(p,&old_count,q);
    assert(!memcmp(&queues.out[0].queueFamilyProperties,q,sizeof(*q)) && queues.out[0].pNext==&unknown);
    capacity=0;queues.out[0].queueFamilyProperties.queueCount=0xdead;vkGetPhysicalDeviceQueueFamilyProperties2(p,&capacity,queues.out);assert(!capacity && queues.out[0].queueFamilyProperties.queueCount==0xdead);
    uint32_t group_count=0;OK(vkEnumeratePhysicalDeviceGroups(instance,&group_count,NULL));assert(group_count>0);
    VkPhysicalDeviceGroupProperties group={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES,.pNext=&unknown};group_count=1;OK(vkEnumeratePhysicalDeviceGroups(instance,&group_count,&group));
    assert(group.physicalDeviceCount==1 && group.physicalDevices[0]==p);
    // Native device-selection layers may copy group structs and overwrite
    // pNext; subsetAllocation is a driver capability, not a group-size rule.
    // Keep the exact guest ABI/canary contract strict without requiring native
    // loaders to implement Trames's profile or preserve an unknown group chain.
    assert(group.subsetAllocation==VK_FALSE || group.subsetAllocation==VK_TRUE);
    if(bridge)assert(!group.subsetAllocation && group.pNext==&unknown);
    group_count=0;assert(vkEnumeratePhysicalDeviceGroups(instance,&group_count,&group)==VK_INCOMPLETE);
    if(bridge){
        VkPhysicalDeviceExternalBufferInfo eb={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
        VkExternalBufferProperties bp={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};vkGetPhysicalDeviceExternalBufferProperties(p,&eb,&bp);assert(!bp.externalMemoryProperties.externalMemoryFeatures && !bp.externalMemoryProperties.compatibleHandleTypes);
        VkPhysicalDeviceExternalFenceInfo ef={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO,.handleType=VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT};VkExternalFenceProperties fp={.sType=VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES};vkGetPhysicalDeviceExternalFenceProperties(p,&ef,&fp);assert(!fp.externalFenceFeatures && !fp.compatibleHandleTypes);
        VkPhysicalDeviceExternalSemaphoreInfo es={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};VkExternalSemaphoreProperties sp={.sType=VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};vkGetPhysicalDeviceExternalSemaphoreProperties(p,&es,&sp);assert(!sp.externalSemaphoreFeatures && !sp.compatibleHandleTypes);
    }
    // Independent Mesa25.1.9 hard requirements, not a claim of GL conformance.
    const char *zink_extensions[]={VK_KHR_MAINTENANCE_1_EXTENSION_NAME,VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME};
    uint32_t ec=0;OK(vkEnumerateDeviceExtensionProperties(p,NULL,&ec,NULL));assert(ec<512);VkExtensionProperties *extensions=calloc(ec,sizeof(*extensions));assert(extensions);OK(vkEnumerateDeviceExtensionProperties(p,NULL,&ec,extensions));
    for(unsigned i=0;i<sizeof(zink_extensions)/sizeof(*zink_extensions);i++){
        int available=0;for(unsigned j=0;j<ec;j++)if(!strcmp(zink_extensions[i],extensions[j].extensionName))available=1;
        printf("zink-required %s %s\n",zink_extensions[i],available?"available":"missing");
    }
    free(extensions);
    puts("instance queries: legacy equivalence, aliases, groups, unknown pNext and capacity guards: ok");
    const char *device_extensions[]={VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME};
    float priority=1;VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&priority};
    features.pNext=NULL;
    VkDeviceCreateInfo create={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&features,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue,.enabledExtensionCount=2,.ppEnabledExtensionNames=device_extensions};
    VkDevice device;OK(vkCreateDevice(p,&create,NULL,&device));
    assert(!vkGetDeviceProcAddr(device,"vkEnumeratePhysicalDeviceGroups"));
    VkBufferCreateInfo buffer_info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=4096,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};VkBuffer buffer;OK(vkCreateBuffer(device,&buffer_info,NULL,&buffer));
    VkMemoryRequirements requirements={0};vkGetBufferMemoryRequirements(device,buffer,&requirements);
    VkBufferMemoryRequirementsInfo2 buffer2={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=buffer};
    VkMemoryRequirements2 req2={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&unknown};
    PFN_vkGetBufferMemoryRequirements2KHR buffer_req=(PFN_vkGetBufferMemoryRequirements2KHR)vkGetDeviceProcAddr(device,"vkGetBufferMemoryRequirements2KHR");assert(buffer_req);buffer_req(device,&buffer2,&req2);
    assert(same_requirements(req2.memoryRequirements,requirements) && req2.pNext==&unknown);
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={8,8,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};VkImage image;OK(vkCreateImage(device,&ci,NULL,&image));
    vkGetImageMemoryRequirements(device,image,&requirements);
    VkImageMemoryRequirementsInfo2 image2={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,.image=image};
    PFN_vkGetImageMemoryRequirements2KHR image_req=(PFN_vkGetImageMemoryRequirements2KHR)vkGetDeviceProcAddr(device,"vkGetImageMemoryRequirements2KHR");assert(image_req);image_req(device,&image2,&req2);assert(same_requirements(req2.memoryRequirements,requirements));
    VkImageSparseMemoryRequirementsInfo2 sparse={.sType=VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2,.image=image};
    PFN_vkGetImageSparseMemoryRequirements2KHR sparse_req=(PFN_vkGetImageSparseMemoryRequirements2KHR)vkGetDeviceProcAddr(device,"vkGetImageSparseMemoryRequirements2KHR");assert(sparse_req);count=12;sparse_req(device,&sparse,&count,NULL);assert(!count);
    assert(unknown.canary==0xcafe123456789abcULL);
    vkDestroyImage(device,image,NULL);vkDestroyBuffer(device,buffer,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    puts("vulkan-queries-ok: extension-only device, memory requirements and unavailable external handles");return 0;
}

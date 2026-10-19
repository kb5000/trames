// SPDX-License-Identifier: GPL-3.0-or-later
// Independent standard Vulkan GPU oracle for bounded, offset image copies.
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(VkResult r){if(r!=VK_SUCCESS){fprintf(stderr,"vulkan-copy VkResult=%d\n",r);exit(1);}}
static VkDevice device;
static VkPhysicalDeviceMemoryProperties types;
static VkDeviceMemory allocate(VkMemoryRequirements r,VkMemoryPropertyFlags flags){
    uint32_t i=0;while(i<types.memoryTypeCount && (!(r.memoryTypeBits&(1u<<i)) || (types.memoryTypes[i].propertyFlags&flags)!=flags))i++;
    if(i==types.memoryTypeCount){fputs("vulkan-copy: memory type unavailable\n",stderr);exit(1);}
    VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.size,.memoryTypeIndex=i};VkDeviceMemory m;check(vkAllocateMemory(device,&a,NULL,&m));return m;
}
static void barrier(VkCommandBuffer c,VkImage image,VkImageLayout before,VkImageLayout after,VkAccessFlags src,VkAccessFlags dst){
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=src,.dstAccessMask=dst,.oldLayout=before,.newLayout=after,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
}
int main(void){
    VkInstance instance;VkInstanceCreateInfo in={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};check(vkCreateInstance(&in,NULL,&instance));
    uint32_t count=1;VkPhysicalDevice physical;check(vkEnumeratePhysicalDevices(instance,&count,&physical));vkGetPhysicalDeviceMemoryProperties(physical,&types);
    VkQueueFamilyProperties families[32];count=32;vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);uint32_t family=0;
    while(family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;
    if(family==count)return 1;
    float priority=1;VkDeviceQueueCreateInfo q={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo d={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&q};check(vkCreateDevice(physical,&d,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkImage images[2];VkDeviceMemory memory[2];VkMemoryRequirements r;
    for(unsigned i=0;i<2;i++){
        VkImageCreateInfo info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={8,8,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        check(vkCreateImage(device,&info,NULL,&images[i]));vkGetImageMemoryRequirements(device,images[i],&r);memory[i]=allocate(r,0);check(vkBindImageMemory(device,images[i],memory[i],0));
    }
    VkBuffer buffer;VkBufferCreateInfo b={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=256,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};check(vkCreateBuffer(device,&b,NULL,&buffer));vkGetBufferMemoryRequirements(device,buffer,&r);VkDeviceMemory output=allocate(r,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(vkBindBufferMemory(device,buffer,output,0));
    VkCommandPool pool;VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};check(vkCreateCommandPool(device,&pc,NULL,&pool));VkCommandBuffer c;VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};check(vkAllocateCommandBuffers(device,&ca,&c));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(c,&begin));
    const VkClearColorValue colors[2]={{{1,1,0,1}},{{0,0,1,1}}};VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    for(unsigned i=0;i<2;i++){barrier(c,images[i],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);vkCmdClearColorImage(c,images[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&colors[i],1,&range);}
    barrier(c,images[0],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkImageCopy copies[2]={
        {.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.srcOffset={1,1,0},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstOffset={2,2,0},.extent={4,4,1}},
        {.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.srcOffset={6,6,0},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstOffset={0,0,0},.extent={2,2,1}}};
    vkCmdCopyImage(c,images[0],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images[1],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,2,copies);
    barrier(c,images[1],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy within={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstOffset={4,0,0},.extent={2,2,1}};
    vkCmdCopyImage(c,images[1],VK_IMAGE_LAYOUT_GENERAL,images[1],VK_IMAGE_LAYOUT_GENERAL,1,&within);
    barrier(c,images[1],VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy read={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={8,8,1}};vkCmdCopyImageToBuffer(c,images[1],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&read);
    check(vkEndCommandBuffer(c));VkFence fence;VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device,&fc,NULL,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&c};check(vkQueueSubmit(queue,1,&submit,fence));check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));
    unsigned char *pixels;check(vkMapMemory(device,output,0,256,0,(void **)&pixels));
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
        int yellow=(x>=2 && x<6 && y>=2 && y<6) || (x<2 && y<2) || (x>=4 && x<6 && y<2);
        unsigned char expected[4]={yellow?255:0,yellow?255:0,yellow?0:255,255};
        if(memcmp(pixels+(y*8+x)*4,expected,4)){fprintf(stderr,"vulkan-copy: incorrect pixel at %u,%u\n",x,y);return 1;}
    }
    vkUnmapMemory(device,output);vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,output,NULL);for(unsigned i=0;i<2;i++){vkDestroyImage(device,images[i],NULL);vkFreeMemory(device,memory[i],NULL);}vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    puts("vulkan-copy-ok: 64 GPU pixels; offset multi-region copies, same-image disjoint copy and teardown");return 0;
}

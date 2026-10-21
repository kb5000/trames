// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary Vulkan API peer: allocations plus coherent mappings share a quota.
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define OK(call) do { VkResult r=(call); if(r!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#call,r);abort();} } while(0)
#define COUNT 8
#define BYTES (16UL<<20)
int main(int argc,char **argv) {
    assert(argc<=2);uint64_t limit=argc==2?strtoull(argv[1],NULL,10):0;
    VkInstance instance;VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};OK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t count=1;VkPhysicalDevice physical;OK(vkEnumeratePhysicalDevices(instance,&count,&physical));assert(count==1);
    VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);assert(memory.memoryHeapCount && memory.memoryTypeCount);
    for(uint32_t i=0;i<memory.memoryHeapCount;i++)assert(memory.memoryHeaps[i].size && (!limit || memory.memoryHeaps[i].size<=limit));
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,NULL);assert(families && families<=64);
    VkQueueFamilyProperties queues[64];vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,queues);
    uint32_t family=0;while(family<families && !(queues[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;assert(family<families);
    const float priority=1;VkDeviceQueueCreateInfo q={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&q};VkDevice device;OK(vkCreateDevice(physical,&dci,NULL,&device));
    for(unsigned round=0;round<2;round++){
        VkBuffer buffers[COUNT];VkDeviceMemory allocations[COUNT];volatile uint8_t *mapped[COUNT];
        for(unsigned i=0;i<COUNT;i++){
            VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=BYTES,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
            OK(vkCreateBuffer(device,&bci,NULL,&buffers[i]));VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffers[i],&req);
            uint32_t type=0;VkMemoryPropertyFlags coherent=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            while(type<memory.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || (memory.memoryTypes[type].propertyFlags&coherent)!=coherent))type++;
            assert(type<memory.memoryTypeCount);
            VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};OK(vkAllocateMemory(device,&ai,NULL,&allocations[i]));
            OK(vkBindBufferMemory(device,buffers[i],allocations[i],0));void *address;OK(vkMapMemory(device,allocations[i],0,BYTES,0,&address));mapped[i]=address;
            mapped[i][0]=(uint8_t)(i+round*COUNT);mapped[i][BYTES-1]=(uint8_t)(255-i-round*COUNT);
        }
        for(unsigned i=0;i<COUNT;i++){
            assert(mapped[i][0]==i+round*COUNT && mapped[i][BYTES-1]==255-i-round*COUNT);
            vkUnmapMemory(device,allocations[i]);vkDestroyBuffer(device,buffers[i],NULL);vkFreeMemory(device,allocations[i],NULL);
        }
    }
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    printf("vulkan-budget-ok: two rounds, 8 simultaneous 16 MiB coherent mappings, heap limit %llu\n",(unsigned long long)limit);return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#define OK(expr) do {VkResult result=(expr);if(result!=VK_SUCCESS){fprintf(stderr,"%s: VkResult=%d\n",#expr,result);abort();}}while(0)
#define N 4096u
int main(int argc,char **argv) {
    assert(argc==2 || argc==3);
    FILE *file=fopen(argv[1],"rb");assert(file);assert(fseek(file,0,SEEK_END)==0);long size=ftell(file);assert(size>0 && size%4==0);rewind(file);uint32_t *code=malloc((size_t)size);assert(code && fread(code,1,(size_t)size,file)==(size_t)size);fclose(file);
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Trames compute lab",.apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo instance_info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance;
    if(argc==3 && !strcmp(argv[2],"disabled")) {assert(vkCreateInstance(&instance_info,NULL,&instance)==VK_ERROR_INCOMPATIBLE_DRIVER);free(code);puts("vulkan-disabled-ok");return 0;}
    OK(vkCreateInstance(&instance_info,NULL,&instance));
    uint32_t physical_count=0;OK(vkEnumeratePhysicalDevices(instance,&physical_count,NULL));assert(physical_count>0);
    VkPhysicalDevice *physicals=calloc(physical_count,sizeof(*physicals));assert(physicals);OK(vkEnumeratePhysicalDevices(instance,&physical_count,physicals));VkPhysicalDevice physical=physicals[0];free(physicals);
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);
    fprintf(stderr,"Vulkan driver: %s (type=%u)\n",props.deviceName,props.deviceType);
    uint32_t family_count=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,NULL);assert(family_count && family_count<64);VkQueueFamilyProperties families[64];vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,families);
    uint32_t family=0;while(family<family_count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;assert(family<family_count);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;OK(vkCreateDevice(physical,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    assert(vkGetDeviceProcAddr(device,"vkCmdDispatch"));assert(!vkGetDeviceProcAddr(device,"vkAbsolutelyNotARealFunction"));
    assert(!vkGetDeviceProcAddr(device,"vkCreateInstance"));
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=N*4,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};VkBuffer buffer;OK(vkCreateBuffer(device,&bi,NULL,&buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);VkPhysicalDeviceMemoryProperties memprops;vkGetPhysicalDeviceMemoryProperties(physical,&memprops);uint32_t type=0;while(type<memprops.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || !(memprops.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)))type++;assert(type<memprops.memoryTypeCount);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};VkDeviceMemory memory;OK(vkAllocateMemory(device,&ai,NULL,&memory));OK(vkBindBufferMemory(device,buffer,memory,0));uint32_t *values;OK(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,(void **)&values));
    for(uint32_t i=0;i<N;i++)values[i]=i;
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=memory,.size=VK_WHOLE_SIZE};OK(vkFlushMappedMemoryRanges(device,1,&range));
    VkShaderModuleCreateInfo si={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)size,.pCode=code};VkShaderModule shader;OK(vkCreateShaderModule(device,&si,NULL,&shader));free(code);
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};VkDescriptorSetLayout descriptor_layout;OK(vkCreateDescriptorSetLayout(device,&li,NULL,&descriptor_layout));
    VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=4};VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&descriptor_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};VkPipelineLayout layout;OK(vkCreatePipelineLayout(device,&pli,NULL,&layout));
    VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=layout,.basePipelineIndex=-1};VkPipeline pipeline;OK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&pipeline));vkDestroyShaderModule(device,shader,NULL);
    VkDescriptorPoolSize poolsize={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&poolsize};VkDescriptorPool descriptor_pool;OK(vkCreateDescriptorPool(device,&dpi,NULL,&descriptor_pool));VkDescriptorSetAllocateInfo dsa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&descriptor_layout};VkDescriptorSet set;OK(vkAllocateDescriptorSets(device,&dsa,&set));
    VkDescriptorBufferInfo db={.buffer=buffer,.range=VK_WHOLE_SIZE};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&db};vkUpdateDescriptorSets(device,1,&write,0,NULL);
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool command_pool;OK(vkCreateCommandPool(device,&cpi,NULL,&command_pool));VkCommandBufferAllocateInfo cba={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;OK(vkAllocateCommandBuffers(device,&cba,&command));
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;OK(vkCreateFence(device,&fi,NULL,&fence));
    for(uint32_t round=0;round<3;round++) {
        if(round){OK(vkResetFences(device,1,&fence));OK(vkResetCommandBuffer(command,0));}
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};OK(vkBeginCommandBuffer(command,&begin));vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);uint32_t factor=3;vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&factor);vkCmdDispatch(command,N/64,1,1);
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(vkEndCommandBuffer(command));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};OK(vkQueueSubmit(queue,1,&submit,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000UL));OK(vkInvalidateMappedMemoryRanges(device,1,&range));
        for(uint32_t i=0;i<N;i++){uint32_t wanted=i;for(uint32_t j=0;j<=round;j++)wanted=wanted*3+7;assert(values[i]==wanted);}printf("compute round %u: %u uint32 values correct\n",round+1,N);
    }
    // Unsigned fences must time out rather than block the entire interpreter.
    OK(vkResetFences(device,1,&fence));assert(vkWaitForFences(device,1,&fence,VK_TRUE,0)==VK_TIMEOUT);assert(vkWaitForFences(device,1,&fence,VK_TRUE,1000000)==VK_TIMEOUT);
    OK(vkQueueWaitIdle(queue));OK(vkDeviceWaitIdle(device));vkUnmapMemory(device,memory);vkDestroyFence(device,fence,NULL);vkFreeCommandBuffers(device,command_pool,1,&command);vkDestroyCommandPool(device,command_pool,NULL);OK(vkFreeDescriptorSets(device,descriptor_pool,1,&set));vkDestroyDescriptorPool(device,descriptor_pool,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,descriptor_layout,NULL);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,memory,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    puts("vulkan-compute-ok");return 0;
}

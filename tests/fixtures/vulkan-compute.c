// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#define OK(expr) do {VkResult result=(expr);if(result!=VK_SUCCESS){fprintf(stderr,"%s: VkResult=%d\n",#expr,result);abort();}}while(0)
#define N 4096u
static _Atomic unsigned compile_active,peer_ready,peer_ticks;
struct compile_args {VkDevice device;VkComputePipelineCreateInfo info;VkPipeline output;};
static void *compile_pipeline(void *data) {
    struct compile_args *args=data;
    OK(vkCreateComputePipelines(args->device,VK_NULL_HANDLE,1,&args->info,NULL,&args->output));
    return NULL;
}
static void *compile_peer(void *unused) {
    (void)unused;atomic_store(&peer_ready,1);
    while(atomic_load(&compile_active))atomic_fetch_add(&peer_ticks,1);
    return NULL;
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==3);
    int bridge_checks=argc==3 && !strcmp(argv[2],"bridge-check");
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
    pthread_t peer;atomic_store(&compile_active,1);assert(pthread_create(&peer,NULL,compile_peer,NULL)==0);
    while(!atomic_load(&peer_ready))sched_yield();
    VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=layout,.basePipelineIndex=-1};VkPipeline pipeline;
    // IDs are deliberately out of order; bytes are not at offset zero.
    uint32_t constants[4]={0xabcdef,2,11,0x123456};VkSpecializationMapEntry entries[2]={{1,4,4},{0,8,4}};
    VkSpecializationInfo special={.mapEntryCount=2,.pMapEntries=entries,.dataSize=sizeof(constants),.pData=constants};
    if(bridge_checks){
        pi.stage.pSpecializationInfo=&special;VkPipeline rejected=VK_NULL_HANDLE;
        entries[1].offset=15;assert(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&rejected)==VK_ERROR_UNKNOWN);entries[1].offset=8;
        entries[1].constantID=1;assert(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&rejected)==VK_ERROR_UNKNOWN);entries[1].constantID=0;
    }
    pi.stage.pSpecializationInfo=&special;struct compile_args parallel={.device=device,.info=pi};pthread_t compiler;
    assert(pthread_create(&compiler,NULL,compile_pipeline,&parallel)==0);
    pi.stage.pSpecializationInfo=NULL;OK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&pipeline));
    assert(pthread_join(compiler,NULL)==0);VkPipeline specialized=parallel.output;vkDestroyShaderModule(device,shader,NULL);
    atomic_store(&compile_active,0);assert(pthread_join(peer,NULL)==0);assert(atomic_load(&peer_ticks)>0);
    puts("pipeline compile with active peer: ok");
    puts("parallel pipeline callers preserve default/specialized outputs: ok");
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
    VkSemaphoreCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore semaphores[2];
    for(unsigned i=0;i<2;i++)OK(vkCreateSemaphore(device,&sci,NULL,&semaphores[i]));
    assert(vkGetDeviceProcAddr(device,"vkCreateSemaphore"));
    VkCommandBuffer chain[3];cba.commandBufferCount=3;OK(vkAllocateCommandBuffers(device,&cba,chain));
    for(unsigned i=0;i<3;i++){
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(chain[i],&begin));
        vkCmdBindPipeline(chain[i],VK_PIPELINE_BIND_POINT_COMPUTE,specialized);vkCmdBindDescriptorSets(chain[i],VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
        uint32_t factor=2;vkCmdPushConstants(chain[i],layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&factor);vkCmdDispatch(chain[i],N/64,1,1);
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(chain[i],VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(vkEndCommandBuffer(chain[i]));
    }
    VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if(bridge_checks){
        VkSubmitInfo invalid={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&semaphores[0],.pWaitDstStageMask=&wait_stage};
        assert(vkQueueSubmit(queue,1,&invalid,VK_NULL_HANDLE)==VK_ERROR_UNKNOWN);
    }
    for(unsigned round=0;round<2;round++){
        OK(vkResetFences(device,1,&fence));
        VkSubmitInfo first={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&chain[0],.signalSemaphoreCount=1,.pSignalSemaphores=&semaphores[0]};
        OK(vkQueueSubmit(queue,1,&first,VK_NULL_HANDLE));
        if(bridge_checks){
            VkPipelineStageFlags unsupported_stage=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            VkSubmitInfo invalid={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&semaphores[0],.pWaitDstStageMask=&unsupported_stage};
            assert(vkQueueSubmit(queue,1,&invalid,VK_NULL_HANDLE)==VK_ERROR_FEATURE_NOT_PRESENT);
        }
        VkSubmitInfo rest[2]={
            {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&semaphores[0],.pWaitDstStageMask=&wait_stage,.commandBufferCount=1,.pCommandBuffers=&chain[1],.signalSemaphoreCount=1,.pSignalSemaphores=&semaphores[1]},
            {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&semaphores[1],.pWaitDstStageMask=&wait_stage,.commandBufferCount=1,.pCommandBuffers=&chain[2]}
        };
        OK(vkQueueSubmit(queue,2,rest,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000UL));OK(vkInvalidateMappedMemoryRanges(device,1,&range));
        for(uint32_t i=0;i<N;i++){uint32_t wanted=i;for(unsigned j=0;j<3;j++)wanted=wanted*3+7;for(unsigned j=0;j<(round+1)*3;j++)wanted=(wanted*2+11)*2;assert(values[i]==wanted);}
        printf("compute semaphore chain %u: %u specialized uint32 values correct\n",round+1,N);
    }
    // Semaphore-only submissions retain their ordering without command buffers.
    OK(vkResetFences(device,1,&fence));
    VkSubmitInfo empty[2]={
        {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.signalSemaphoreCount=1,.pSignalSemaphores=&semaphores[0]},
        {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&semaphores[0],.pWaitDstStageMask=&wait_stage}
    };
    OK(vkQueueSubmit(queue,2,empty,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000UL));
    puts("binary semaphore reuse and empty submissions: ok");
    for(unsigned i=0;i<2;i++)vkDestroySemaphore(device,semaphores[i],NULL);
    vkFreeCommandBuffers(device,command_pool,3,chain);vkDestroyPipeline(device,specialized,NULL);
    // A partial mapping has its own guest address; Vulkan offsets still refer
    // to the full allocation. Flush/invalidate must translate both correctly.
    uint32_t saved=values[0];vkUnmapMemory(device,memory);
    VkDeviceSize atom=props.limits.nonCoherentAtomSize;
    VkDeviceSize offset=atom<req.size?atom:0;
    VkDeviceSize length=req.size-offset;uint32_t *partial;
    OK(vkMapMemory(device,memory,offset,VK_WHOLE_SIZE,0,(void **)&partial));
    VkMappedMemoryRange part={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=memory,.offset=offset,.size=VK_WHOLE_SIZE};
    OK(vkInvalidateMappedMemoryRanges(device,1,&part));
    uint32_t index=(uint32_t)(offset/4),wanted=index;for(unsigned j=0;j<3;j++)wanted=wanted*3+7;for(unsigned j=0;j<6;j++)wanted=(wanted*2+11)*2;
    assert(length>=4 && (index>=N || partial[0]==wanted));partial[0]=0x12345678;
    OK(vkFlushMappedMemoryRanges(device,1,&part));vkUnmapMemory(device,memory);
    OK(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,(void **)&values));OK(vkInvalidateMappedMemoryRanges(device,1,&range));
    assert(values[index]==0x12345678 && (offset==0 || values[0]==saved));
    puts("partial mapping, offset flush/invalidate and remap persistence: ok");
    vkUnmapMemory(device,memory);void *unaligned;
    OK(vkMapMemory(device,memory,3,req.size-3,0,&unaligned));
    assert(((uintptr_t)unaligned-3)%props.limits.minMemoryMapAlignment==0);
    vkUnmapMemory(device,memory);OK(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,(void **)&values));
    puts("unaligned map offset preserves Vulkan pointer alignment: ok");
    // Unsigned fences must time out rather than block the entire interpreter.
    OK(vkResetFences(device,1,&fence));assert(vkWaitForFences(device,1,&fence,VK_TRUE,0)==VK_TIMEOUT);assert(vkWaitForFences(device,1,&fence,VK_TRUE,1000000)==VK_TIMEOUT);
    OK(vkQueueWaitIdle(queue));OK(vkDeviceWaitIdle(device));vkUnmapMemory(device,memory);vkDestroyFence(device,fence,NULL);vkFreeCommandBuffers(device,command_pool,1,&command);vkDestroyCommandPool(device,command_pool,NULL);OK(vkFreeDescriptorSets(device,descriptor_pool,1,&set));vkDestroyDescriptorPool(device,descriptor_pool,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,descriptor_layout,NULL);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,memory,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    puts("vulkan-compute-ok");return 0;
}

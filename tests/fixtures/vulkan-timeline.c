// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary Vulkan application: real driver queries, host/thread synchronization,
// mixed binary/timeline GPU submits and coherent memory visibility without fences.
#include <vulkan/vulkan.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define OK(call) do {VkResult r=(call);if(r!=VK_SUCCESS){fprintf(stderr,"%s: %d at %d\n",#call,r,__LINE__);abort();}}while(0)
static PFN_vkGetSemaphoreCounterValueKHR get_counter;
static PFN_vkSignalSemaphoreKHR signal_semaphore;
static PFN_vkWaitSemaphoresKHR wait_semaphores;
static PFN_vkTrimCommandPoolKHR trim_pool;
struct waiter {VkDevice device;VkSemaphore semaphore;uint64_t value;};
static void *wait_peer(void *p) {
    struct waiter *w=p;
    VkSemaphoreWaitInfo info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,.semaphoreCount=1,.pSemaphores=&w->semaphore,.pValues=&w->value};
    OK(wait_semaphores(w->device,&info,5000000000ULL));return NULL;
}
static void signal_value(VkDevice d,VkSemaphore s,uint64_t value) {
    VkSemaphoreSignalInfo info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,.semaphore=s,.value=value};OK(signal_semaphore(d,&info));
}
static void *signal_peer(void *p) {
    struct waiter *w=p;struct timespec delay={0,10000000};nanosleep(&delay,NULL);
    signal_value(w->device,w->semaphore,w->value);return NULL;
}
int main(int argc,char **argv) {
    int bridge=argc>1 && !strcmp(argv[1],"bridge");
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};VkInstance instance;OK(vkCreateInstance(&instance_info,NULL,&instance));
    uint32_t n=1;VkPhysicalDevice physical;OK(vkEnumeratePhysicalDevices(instance,&n,&physical));assert(n==1);
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&timeline};vkGetPhysicalDeviceFeatures2(physical,&features);assert(timeline.timelineSemaphore==VK_TRUE && features.pNext==&timeline);
    VkPhysicalDeviceTimelineSemaphoreProperties limits={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES};
    VkPhysicalDeviceDriverProperties driver={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,.pNext=&limits};
    VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&driver};vkGetPhysicalDeviceProperties2(physical,&properties);
    assert(driver.driverID && driver.driverName[0] && limits.maxTimelineSemaphoreValueDifference);
    assert(properties.pNext==&driver && driver.pNext==&limits && limits.pNext==NULL);
    if(bridge)assert(!driver.conformanceVersion.major && !driver.conformanceVersion.minor && !driver.conformanceVersion.subminor && !driver.conformanceVersion.patch);
    printf("native-driver-id=%u name=%s info=%s max-timeline-difference=%llu\n",driver.driverID,driver.driverName,driver.driverInfo,(unsigned long long)limits.maxTimelineSemaphoreValueDifference);
    uint32_t count=0;OK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL));VkExtensionProperties *extensions=calloc(count,sizeof(*extensions));assert(extensions);OK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,extensions));
    const char *names[]={VK_KHR_MAINTENANCE_1_EXTENSION_NAME,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME};
    for(unsigned i=0;i<3;i++){unsigned found=0;for(unsigned j=0;j<count;j++)if(!strcmp(names[i],extensions[j].extensionName))found++;assert(found==1);}free(extensions);
    float priority=1;VkDeviceQueueCreateInfo queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo device_info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&timeline,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue_info,.enabledExtensionCount=3,.ppEnabledExtensionNames=names};
    VkDevice device;
    if(bridge){
        timeline.timelineSemaphore=VK_FALSE;OK(vkCreateDevice(physical,&device_info,NULL,&device));
        VkSemaphoreTypeCreateInfo type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE};VkSemaphoreCreateInfo info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};VkSemaphore rejected;
        assert(vkCreateSemaphore(device,&info,NULL,&rejected)==VK_ERROR_FEATURE_NOT_PRESENT);vkDestroyDevice(device,NULL);timeline.timelineSemaphore=VK_TRUE;
    }
    OK(vkCreateDevice(physical,&device_info,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,0,0,&queue);
    assert(vkGetDeviceProcAddr(device,"vkGetSemaphoreCounterValueKHR") && vkGetDeviceProcAddr(device,"vkWaitSemaphoresKHR") && vkGetDeviceProcAddr(device,"vkSignalSemaphoreKHR") && vkGetDeviceProcAddr(device,"vkTrimCommandPoolKHR"));
    get_counter=(PFN_vkGetSemaphoreCounterValueKHR)vkGetDeviceProcAddr(device,"vkGetSemaphoreCounterValueKHR");
    signal_semaphore=(PFN_vkSignalSemaphoreKHR)vkGetDeviceProcAddr(device,"vkSignalSemaphoreKHR");
    wait_semaphores=(PFN_vkWaitSemaphoresKHR)vkGetDeviceProcAddr(device,"vkWaitSemaphoresKHR");
    trim_pool=(PFN_vkTrimCommandPoolKHR)vkGetDeviceProcAddr(device,"vkTrimCommandPoolKHR");
    const uint64_t seed=1ULL<<40;VkSemaphore semaphores[2];
    for(unsigned i=0;i<2;i++){
        VkSemaphoreTypeCreateInfo type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE,.initialValue=seed+i};
        VkSemaphoreCreateInfo info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};OK(vkCreateSemaphore(device,&info,NULL,&semaphores[i]));
    }
    uint64_t value=0;OK(get_counter(device,semaphores[0],&value));assert(value==seed);
    uint64_t targets[]={seed+1,seed+1};VkSemaphoreWaitInfo wait={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,.semaphoreCount=2,.pSemaphores=semaphores,.pValues=targets};
    assert(wait_semaphores(device,&wait,0)==VK_TIMEOUT);wait.flags=VK_SEMAPHORE_WAIT_ANY_BIT;OK(wait_semaphores(device,&wait,0));wait.flags=0;
    assert(wait_semaphores(device,&wait,1000000)==VK_TIMEOUT);
    struct waiter peer={device,semaphores[0],seed+1};pthread_t thread;assert(!pthread_create(&thread,NULL,wait_peer,&peer));
    struct timespec delay={0,10000000};nanosleep(&delay,NULL);signal_value(device,semaphores[0],seed+1);assert(!pthread_join(thread,NULL));OK(wait_semaphores(device,&wait,0));
    puts("timeline host: 64-bit initial/counter, wait-any/all, timeout and cross-thread wakeup: ok");
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=512,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};VkBuffer buffer;OK(vkCreateBuffer(device,&bi,NULL,&buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);unsigned type=0;
    while(type<memory.memoryTypeCount && (!(req.memoryTypeBits&(1U<<type)) || (memory.memoryTypes[type].propertyFlags&6)!=6))type++;
    assert(type<memory.memoryTypeCount);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};VkDeviceMemory backing;OK(vkAllocateMemory(device,&alloc,NULL,&backing));OK(vkBindBufferMemory(device,buffer,backing,0));uint32_t *bytes;OK(vkMapMemory(device,backing,0,VK_WHOLE_SIZE,0,(void **)&bytes));memset(bytes,0,512);
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=0,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};VkCommandPool pool;OK(vkCreateCommandPool(device,&pi,NULL,&pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=2};VkCommandBuffer commands[2];OK(vkAllocateCommandBuffers(device,&ca,commands));
    for(unsigned i=0;i<2;i++){VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(commands[i],&begin));vkCmdFillBuffer(commands[i],buffer,0,256,0x12340000U+i);VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(commands[i],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(vkEndCommandBuffer(commands[i]));}
    trim_pool(device,pool,0); // Must preserve recorded commands.
    VkSemaphoreCreateInfo binary_info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore binary;OK(vkCreateSemaphore(device,&binary_info,NULL,&binary));
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;uint64_t future=seed+2,completed=seed+3,ignored=99;
    VkTimelineSemaphoreSubmitInfo values[2]={{.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,.waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&future,.signalSemaphoreValueCount=1,.pSignalSemaphoreValues=&ignored},{.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,.waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&ignored,.signalSemaphoreValueCount=1,.pSignalSemaphoreValues=&completed}};
    VkSubmitInfo submits[2]={{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&values[0],.waitSemaphoreCount=1,.pWaitSemaphores=&semaphores[0],.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&commands[0],.signalSemaphoreCount=1,.pSignalSemaphores=&binary},{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&values[1],.waitSemaphoreCount=1,.pWaitSemaphores=&binary,.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&commands[1],.signalSemaphoreCount=1,.pSignalSemaphores=&semaphores[1]}};
    peer.value=future;assert(!pthread_create(&thread,NULL,signal_peer,&peer));
    OK(vkQueueSubmit(queue,2,submits,VK_NULL_HANDLE));bytes[100]=0xaabbccdd;assert(!pthread_join(thread,NULL));
    wait.semaphoreCount=1;wait.pSemaphores=&semaphores[1];wait.pValues=&completed;OK(wait_semaphores(device,&wait,5000000000ULL));
    for(unsigned i=0;i<64;i++)assert(bytes[i]==0x12340001);
    assert(bytes[100]==0xaabbccdd);
    OK(get_counter(device,semaphores[1],&value));assert(value>=completed);
    OK(vkResetCommandPool(device,pool,0));trim_pool(device,pool,0);
    puts("timeline GPU: future host signal, mixed binary waits, 64-bit submit values, coherent output and pool reset after wait: ok");
    for(unsigned round=0;round<2;round++){
        if(round)OK(vkResetCommandPool(device,pool,0));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(commands[0],&begin));vkCmdFillBuffer(commands[0],buffer,0,256,0x67890000U+round);
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(commands[0],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(vkEndCommandBuffer(commands[0]));
        uint64_t produced=seed+4+round*2,consumed=produced+1;
        VkTimelineSemaphoreSubmitInfo timeline_values[2]={{.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,.signalSemaphoreValueCount=1,.pSignalSemaphoreValues=&produced},{.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,.waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&produced,.signalSemaphoreValueCount=1,.pSignalSemaphoreValues=&consumed}};
        VkSubmitInfo ordered[2]={{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline_values[0],.signalSemaphoreCount=1,.pSignalSemaphores=&semaphores[0]},{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline_values[1],.waitSemaphoreCount=1,.pWaitSemaphores=&semaphores[0],.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&commands[0],.signalSemaphoreCount=1,.pSignalSemaphores=&semaphores[1]}};
        if(round){OK(vkQueueSubmit(queue,1,&ordered[0],VK_NULL_HANDLE));OK(vkQueueSubmit(queue,1,&ordered[1],VK_NULL_HANDLE));}
        else OK(vkQueueSubmit(queue,2,ordered,VK_NULL_HANDLE));
        wait.pValues=&consumed;OK(wait_semaphores(device,&wait,5000000000ULL));
        for(unsigned i=0;i<64;i++)assert(bytes[i]==0x67890000U+round);
    }
    puts("timeline GPU: inter-batch and previously queued signal/wait dependencies: ok");
    OK(vkDeviceWaitIdle(device));vkDestroyCommandPool(device,pool,NULL);vkDestroySemaphore(device,binary,NULL);for(unsigned i=0;i<2;i++)vkDestroySemaphore(device,semaphores[i],NULL);vkUnmapMemory(device,backing);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,backing,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);puts("vulkan-timeline-ok");return 0;
}

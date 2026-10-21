// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary Vulkan loader client: same executable source on host and guest.
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(expr) do { VkResult r=(expr);if(r!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#expr,r);abort();} } while(0)
static VkDevice device;
static VkPhysicalDevice physical;
static struct allocation { VkBuffer buffer;VkDeviceMemory memory;unsigned char *data; } allocations[3];
static void buffer(unsigned slot,VkBufferUsageFlags usage) {
    struct allocation *a=allocations+slot;
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=1024,.usage=usage};
    OK(vkCreateBuffer(device,&info,NULL,&a->buffer));VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,a->buffer,&req);
    VkPhysicalDeviceMemoryProperties props;vkGetPhysicalDeviceMemoryProperties(physical,&props);unsigned i;
    for(i=0;i<props.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&6)==6)break;
    assert(i<props.memoryTypeCount);VkMemoryAllocateInfo mem={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=i};
    OK(vkAllocateMemory(device,&mem,NULL,&a->memory));OK(vkBindBufferMemory(device,a->buffer,a->memory,0));OK(vkMapMemory(device,a->memory,0,1024,0,(void **)&a->data));memset(a->data,0xa5,1024);
}
int main(int argc,char **argv) {
    assert(argc==2);VkInstance instance;VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char *query=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;ii.enabledExtensionCount=1;ii.ppEnabledExtensionNames=&query;OK(vkCreateInstance(&ii,NULL,&instance));
    uint32_t count=1;OK(vkEnumeratePhysicalDevices(instance,&count,&physical));
    uint32_t n=0;OK(vkEnumerateDeviceExtensionProperties(physical,NULL,&n,NULL));VkExtensionProperties *extensions=calloc(n,sizeof(*extensions));assert(extensions);OK(vkEnumerateDeviceExtensionProperties(physical,NULL,&n,extensions));
    int feedback=0;for(unsigned i=0;i<n;i++)if(!strcmp(extensions[i].extensionName,VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME))feedback=1;free(extensions);
    VkPhysicalDeviceTransformFeedbackFeaturesEXT tf={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&tf};
    PFN_vkGetPhysicalDeviceFeatures2KHR get_features=(void *)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceFeatures2KHR");assert(get_features);get_features(physical,&features);assert(!feedback || tf.transformFeedback);
    VkPhysicalDeviceTransformFeedbackPropertiesEXT limits={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&limits};
    PFN_vkGetPhysicalDeviceProperties2KHR get_props=(void *)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceProperties2KHR");assert(get_props);get_props(physical,&props);
    if(feedback)assert(limits.maxTransformFeedbackBuffers>=2 && limits.transformFeedbackQueries);
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,NULL);VkQueueFamilyProperties *queues=calloc(families,sizeof(*queues));assert(queues);vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,queues);
    uint32_t family=0;while(family<families && !(queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<families && queues[family].timestampValidBits);free(queues);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *extension=VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;tf.transformFeedback=feedback;tf.geometryStreams=VK_FALSE;
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=feedback?&tf:NULL,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=feedback,.ppEnabledExtensionNames=&extension};OK(vkCreateDevice(physical,&di,NULL,&device));
    buffer(0,VK_BUFFER_USAGE_TRANSFER_DST_BIT);if(feedback){buffer(1,VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT);buffer(2,VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT);}
    VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);VkCommandPool pool;VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};OK(vkCreateCommandPool(device,&pi,NULL,&pool));
    VkCommandBuffer c;VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.commandBufferCount=1};OK(vkAllocateCommandBuffers(device,&ca,&c));
    VkQueryPool time_pool,occlusion_pool,tf_pool=VK_NULL_HANDLE;VkQueryPoolCreateInfo qc={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=2};OK(vkCreateQueryPool(device,&qc,NULL,&time_pool));qc.queryType=VK_QUERY_TYPE_OCCLUSION;qc.queryCount=1;OK(vkCreateQueryPool(device,&qc,NULL,&occlusion_pool));
    VkPipeline pipeline=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;VkShaderModule module=VK_NULL_HANDLE;VkRenderPass pass=VK_NULL_HANDLE;VkFramebuffer fb=VK_NULL_HANDLE;
        VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.subpassCount=1,.pSubpasses=&sub};OK(vkCreateRenderPass(device,&rp,NULL,&pass));
        VkFramebufferCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.width=8,.height=8,.layers=1};OK(vkCreateFramebuffer(device,&fc,NULL,&fb));
    PFN_vkCmdBindTransformFeedbackBuffersEXT bind_tf=NULL;PFN_vkCmdBeginTransformFeedbackEXT begin_tf=NULL;PFN_vkCmdEndTransformFeedbackEXT end_tf=NULL;
    PFN_vkCmdBeginQueryIndexedEXT begin_query=NULL;PFN_vkCmdEndQueryIndexedEXT end_query=NULL;
    if(feedback){
        bind_tf=(void *)vkGetDeviceProcAddr(device,"vkCmdBindTransformFeedbackBuffersEXT");begin_tf=(void *)vkGetDeviceProcAddr(device,"vkCmdBeginTransformFeedbackEXT");end_tf=(void *)vkGetDeviceProcAddr(device,"vkCmdEndTransformFeedbackEXT");
        begin_query=(void *)vkGetDeviceProcAddr(device,"vkCmdBeginQueryIndexedEXT");end_query=(void *)vkGetDeviceProcAddr(device,"vkCmdEndQueryIndexedEXT");assert(bind_tf && begin_tf && end_tf && begin_query && end_query);
        FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0 && !(size%4));rewind(f);uint32_t *code=malloc(size);assert(code && fread(code,1,size,f)==(size_t)size);fclose(f);
        VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=size,.pCode=code};OK(vkCreateShaderModule(device,&sm,NULL,&module));free(code);
        VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};OK(vkCreatePipelineLayout(device,&pl,NULL,&layout));
        VkPipelineShaderStageCreateInfo stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=module,.pName="main"};
        VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
        VkViewport viewport={0,0,8,8,0,1};VkRect2D scissor={{0,0},{8,8}};VkPipelineViewportStateCreateInfo vs={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
        VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.rasterizerDiscardEnable=VK_TRUE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
        VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};VkGraphicsPipelineCreateInfo pc={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=1,.pStages=&stage,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vs,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&bs,.layout=layout,.renderPass=pass};
        OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pc,NULL,&pipeline));qc.queryType=VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;qc.queryCount=1;OK(vkCreateQueryPool(device,&qc,NULL,&tf_pool));
    }
    VkCommandBufferBeginInfo cb={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(c,&cb));vkCmdResetQueryPool(c,time_pool,0,2);vkCmdResetQueryPool(c,occlusion_pool,0,1);if(feedback)vkCmdResetQueryPool(c,tf_pool,0,1);
    vkCmdWriteTimestamp(c,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,time_pool,0);
    VkRenderPassBeginInfo pb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{8,8}}};vkCmdBeginRenderPass(c,&pb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBeginQuery(c,occlusion_pool,0,0);
    if(feedback){
        vkCmdBindPipeline(c,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        VkBuffer buffers[]={allocations[1].buffer,allocations[2].buffer};VkDeviceSize offsets[]={16,16},sizes[]={144,48};bind_tf(c,0,2,buffers,offsets,sizes);
        begin_query(c,tf_pool,0,0,0);begin_tf(c,0,0,NULL,NULL);vkCmdDraw(c,3,2,2,4);end_tf(c,0,0,NULL,NULL);end_query(c,tf_pool,0,0);
    }
    vkCmdEndQuery(c,occlusion_pool,0);vkCmdEndRenderPass(c);
    if(feedback)vkCmdCopyQueryPoolResults(c,tf_pool,0,1,allocations[0].buffer,128,24,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    vkCmdWriteTimestamp(c,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,time_pool,1);vkCmdCopyQueryPoolResults(c,time_pool,0,2,allocations[0].buffer,0,24,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|(feedback?VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT:0),.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(vkEndCommandBuffer(c));
    VkFence fence;VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};OK(vkCreateFence(device,&fi,NULL,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&c};OK(vkQueueSubmit(queue,1,&submit,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));
    uint64_t results[6];memset(results,0xa5,sizeof(results));OK(vkGetQueryPoolResults(device,time_pool,0,2,sizeof(results),results,24,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT));
    assert(results[3]>=results[0] && results[1] && results[4] && results[2]==UINT64_C(0xa5a5a5a5a5a5a5a5) && results[5]==results[2]);
    assert(!memcmp(allocations[0].data,results,40));uint32_t occlusion[2]={1,0};OK(vkGetQueryPoolResults(device,occlusion_pool,0,1,sizeof(occlusion),occlusion,8,VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT));assert(occlusion[0]==0 && occlusion[1]);puts("guest-query-ok: native timestamps, nonprecise occlusion, GPU result copy, availability and stride padding");
    if(feedback){
        uint64_t captured[3];OK(vkGetQueryPoolResults(device,tf_pool,0,1,sizeof(captured),captured,24,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_WAIT_BIT));assert(captured[0]==6 && captured[1]==6 && captured[2]);assert(!memcmp(allocations[0].data+128,captured,sizeof(captured)));
        for(unsigned i=0;i<6;i++){
            float got[4],expected[]={2+i%3,4+i/3,(2+i%3)*3+1,17};memcpy(got,allocations[1].data+16+i*24+4,16);assert(!memcmp(got,expected,16));float other;memcpy(&other,allocations[2].data+16+i*8,4);assert(other==102+i%3);
            for(unsigned j=0;j<4;j++)assert(allocations[1].data[16+i*24+j]==0xa5);
        }
        puts("guest-feedback-ok: 30 exact GPU scalars, two strided buffers, instancing, indexed query API and untouched padding");
    }else puts("guest-feedback-unavailable: native driver has no transform feedback; no capture claimed");
    vkDestroyCommandPool(device,pool,NULL);vkDestroyFence(device,fence,NULL);vkDestroyQueryPool(device,time_pool,NULL);vkDestroyQueryPool(device,occlusion_pool,NULL);
    if(feedback){vkDestroyQueryPool(device,tf_pool,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyShaderModule(device,module,NULL);}
    vkDestroyFramebuffer(device,fb,NULL);vkDestroyRenderPass(device,pass,NULL);
    for(unsigned i=0;i<(feedback?3u:1u);i++){vkUnmapMemory(device,allocations[i].memory);vkDestroyBuffer(device,allocations[i].buffer,NULL);vkFreeMemory(device,allocations[i].memory,NULL);}vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);return 0;
}

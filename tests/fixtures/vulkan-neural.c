// SPDX-License-Identifier: GPL-3.0-or-later
// Real-GPU output oracles for the selected low-bit/subgroup/address profile.
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(x) do { VkResult r=(x);if(r!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#x,r);abort();} } while(0)
struct allocation { VkBuffer buffer; VkDeviceMemory memory; void *data; VkDeviceSize size; };
static VkDevice device;
static VkPhysicalDevice physical;
static struct allocation allocate(size_t bytes,int address) {
    struct allocation a={0};
    VkBufferCreateInfo b={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|(address?VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT:0)};
    OK(vkCreateBuffer(device,&b,NULL,&a.buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,a.buffer,&req);a.size=req.size;
    VkPhysicalDeviceMemoryProperties props;vkGetPhysicalDeviceMemoryProperties(physical,&props);unsigned type=0;
    for(;type<props.memoryTypeCount;type++)if((req.memoryTypeBits&(1U<<type)) && (props.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))break;
    assert(type<props.memoryTypeCount);
    VkMemoryAllocateFlagsInfo flags={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkMemoryAllocateInfo m={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=address?&flags:NULL,.allocationSize=req.size,.memoryTypeIndex=type};OK(vkAllocateMemory(device,&m,NULL,&a.memory));
    VkBindBufferMemoryInfo bind={.sType=VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,.buffer=a.buffer,.memory=a.memory};OK(vkBindBufferMemory2(device,1,&bind));
    OK(vkMapMemory(device,a.memory,0,req.size,0,&a.data));memset(a.data,0,req.size);return a;
}
static void release(struct allocation a) {vkUnmapMemory(device,a.memory);vkDestroyBuffer(device,a.buffer,NULL);vkFreeMemory(device,a.memory,NULL);}
static VkShaderModule shader(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0 && !(size%4));rewind(f);uint32_t *data=malloc((size_t)size);assert(data);assert(fread(data,1,(size_t)size,f)==(size_t)size);fclose(f);
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)size,.pCode=data};VkShaderModule module;OK(vkCreateShaderModule(device,&info,NULL,&module));free(data);return module;
}
struct inputs { _Float16 a[64],b[64]; int8_t x[64]; int16_t y[64]; };
struct outputs {float matrix[64];int32_t narrow[64];uint32_t reductions[64],shuffles[64],ballots[64];float total,shared_total;};
_Static_assert(sizeof(struct inputs)==448,"std430 input layout");
_Static_assert(sizeof(struct outputs)==1288,"std430 output layout");
int main(int argc,char **argv) {
    assert(argc==3);
    const char *instance_ext="VK_KHR_get_physical_device_properties2";
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Trames neural compute acceptance",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledExtensionCount=1,.ppEnabledExtensionNames=&instance_ext};VkInstance instance;OK(vkCreateInstance(&ci,NULL,&instance));
    uint32_t count=1;OK(vkEnumeratePhysicalDevices(instance,&count,&physical));assert(count==1);
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);assert(props.apiVersion>=VK_API_VERSION_1_1);printf("device=%s API=%u.%u\n",props.deviceName,VK_API_VERSION_MAJOR(props.apiVersion),VK_API_VERSION_MINOR(props.apiVersion));
    const char *wanted[]={"VK_KHR_storage_buffer_storage_class","VK_KHR_16bit_storage","VK_KHR_8bit_storage","VK_KHR_shader_float16_int8","VK_KHR_shader_float_controls","VK_EXT_shader_atomic_float","VK_KHR_shader_integer_dot_product","VK_KHR_shader_subgroup_extended_types","VK_EXT_subgroup_size_control","VK_KHR_buffer_device_address","VK_KHR_vulkan_memory_model","VK_KHR_uniform_buffer_standard_layout","VK_KHR_variable_pointers","VK_KHR_shader_subgroup_uniform_control_flow","VK_KHR_shader_subgroup_rotate","VK_KHR_bind_memory2","VK_KHR_maintenance3"};
    uint32_t ne=0;OK(vkEnumerateDeviceExtensionProperties(physical,NULL,&ne,NULL));VkExtensionProperties *available=calloc(ne,sizeof(*available));assert(available);OK(vkEnumerateDeviceExtensionProperties(physical,NULL,&ne,available));
    const char *enabled[32];uint32_t n=0;
    for(unsigned i=0;i<sizeof(wanted)/sizeof(*wanted);i++){int found=0;for(unsigned j=0;j<ne;j++)if(!strcmp(wanted[i],available[j].extensionName))found=1;if(i<11)assert(found);if(found)enabled[n++]=wanted[i];}
    for(unsigned j=0;j<ne;j++)if(!strcmp("VK_KHR_portability_subset",available[j].extensionName))enabled[n++]="VK_KHR_portability_subset";
    free(available);
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
#define FEATURE(type,name,stype) type name={.sType=stype,.pNext=features.pNext};features.pNext=&name
    FEATURE(VkPhysicalDevice16BitStorageFeatures,s16,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES);
    FEATURE(VkPhysicalDevice8BitStorageFeatures,s8,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES);
    FEATURE(VkPhysicalDeviceShaderFloat16Int8Features,arithmetic,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES);
    FEATURE(VkPhysicalDeviceShaderAtomicFloatFeaturesEXT,atomic,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT);
    FEATURE(VkPhysicalDeviceShaderIntegerDotProductFeatures,dot,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES);
    FEATURE(VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures,subgroup_types,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES);
    FEATURE(VkPhysicalDeviceSubgroupSizeControlFeatures,subgroup_size,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES);
    FEATURE(VkPhysicalDeviceBufferDeviceAddressFeatures,address,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
    FEATURE(VkPhysicalDeviceVulkanMemoryModelFeatures,model,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES);
    FEATURE(VkPhysicalDeviceUniformBufferStandardLayoutFeatures,uniform_layout,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES);
    FEATURE(VkPhysicalDeviceVariablePointersFeatures,variable,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES);
    FEATURE(VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR,flow,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR);
    FEATURE(VkPhysicalDeviceShaderSubgroupRotateFeaturesKHR,rotate,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES_KHR);
    vkGetPhysicalDeviceFeatures2(physical,&features);
    assert(s16.storageBuffer16BitAccess && s8.storageBuffer8BitAccess && arithmetic.shaderFloat16 && arithmetic.shaderInt8 && features.features.shaderInt16);
    assert(atomic.shaderBufferFloat32AtomicAdd && dot.shaderIntegerDotProduct && subgroup_types.shaderSubgroupExtendedTypes);
    assert(address.bufferDeviceAddress && model.vulkanMemoryModel);
    assert(subgroup_size.subgroupSizeControl && subgroup_size.computeFullSubgroups);
    features.features=(VkPhysicalDeviceFeatures){.shaderInt16=VK_TRUE,.shaderInt64=VK_TRUE};
    VkPhysicalDeviceSubgroupProperties subgroup={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceSubgroupSizeControlProperties sizes={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES,.pNext=&subgroup};
    VkPhysicalDeviceShaderIntegerDotProductProperties dot_props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES,.pNext=&sizes};
    VkPhysicalDeviceFloatControlsProperties floats={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES,.pNext=&dot_props};
    VkPhysicalDeviceMaintenance3Properties maintenance={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES,.pNext=&floats};
    VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&maintenance};vkGetPhysicalDeviceProperties2(physical,&properties);
    assert(subgroup.subgroupSize && (subgroup.supportedStages&VK_SHADER_STAGE_COMPUTE_BIT));assert(maintenance.maxMemoryAllocationSize);
    printf("subgroup=%u required-size-stages=%x packed-int8-accelerated=%u fp16-RTE=%u shared-float-atomics=%u/%u\n",subgroup.subgroupSize,sizes.requiredSubgroupSizeStages,dot_props.integerDotProduct4x8BitPackedSignedAccelerated,floats.shaderRoundingModeRTEFloat16,atomic.shaderSharedFloat32Atomics,atomic.shaderSharedFloat32AtomicAdd);
    uint32_t qn=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&qn,NULL);VkQueueFamilyProperties *queues=calloc(qn,sizeof(*queues));assert(queues);vkGetPhysicalDeviceQueueFamilyProperties(physical,&qn,queues);uint32_t family=0;for(;family<qn;family++)if(queues[family].queueFlags&VK_QUEUE_COMPUTE_BIT)break;assert(family<qn);free(queues);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&features,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=n,.ppEnabledExtensionNames=enabled};OK(vkCreateDevice(physical,&di,NULL,&device));
    vkDestroyDevice(device,NULL);
    VkPhysicalDeviceVulkan11Features aggregate={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 aggregate_query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&aggregate};vkGetPhysicalDeviceFeatures2(physical,&aggregate_query);
    assert(aggregate.storageBuffer16BitAccess==s16.storageBuffer16BitAccess && aggregate.variablePointers==variable.variablePointers);
    VkDeviceCreateInfo aggregate_info=di;aggregate_info.pNext=&aggregate;OK(vkCreateDevice(physical,&aggregate_info,NULL,&device));vkDestroyDevice(device,NULL);
    if(getenv("TRAMES_NEURAL_BRIDGE_CHECKS")){
        VkDevice rejected=VK_NULL_HANDLE;VkBool32 old=address.bufferDeviceAddressCaptureReplay;
        assert(!old);address.bufferDeviceAddressCaptureReplay=VK_TRUE;
        assert(vkCreateDevice(physical,&di,NULL,&rejected)==VK_ERROR_FEATURE_NOT_PRESENT && !rejected);
        address.bufferDeviceAddressCaptureReplay=old;
    }
    OK(vkCreateDevice(physical,&di,NULL,&device));
    VkQueue queue;VkDeviceQueueInfo2 queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,.queueFamilyIndex=family};vkGetDeviceQueue2(device,&queue_info,&queue);
    struct allocation in=allocate(sizeof(struct inputs),0),out=allocate(sizeof(struct outputs),0),pointer=allocate(64*4,1);
    struct inputs *input=in.data;struct outputs *output=out.data;uint32_t *address_data=pointer.data;
    for(unsigned i=0;i<64;i++){input->a[i]=(_Float16)((int)(i%7)-3);input->b[i]=(_Float16)((int)(i%5)-2);input->x[i]=(int8_t)((int)i-32);input->y[i]=(int16_t)((int)i-100);address_data[i]=i*7;}
    VkBufferDeviceAddressInfo ai={.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer=pointer.buffer};PFN_vkGetBufferDeviceAddressKHR get_address=(PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(device,"vkGetBufferDeviceAddressKHR");assert(get_address);VkDeviceAddress gpu_address=get_address(device,&ai);assert(gpu_address);
    VkDescriptorSetLayoutBinding bindings[2]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};VkDescriptorSetLayout set_layout;OK(vkCreateDescriptorSetLayout(device,&li,NULL,&set_layout));
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,8};VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};VkPipelineLayout layout;OK(vkCreatePipelineLayout(device,&pl,NULL,&layout));
    VkShaderModule modules[2]={shader(argv[1]),shader(argv[2])};VkPipeline pipelines[2];
    for(unsigned i=0;i<2;i++){VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.flags=VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT|VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=modules[i],.pName="main"},.layout=layout};if(!i && getenv("TRAMES_NEURAL_BRIDGE_CHECKS") && !sizes.requiredSubgroupSizeStages){
        VkPipelineShaderStageRequiredSubgroupSizeCreateInfo required={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,.requiredSubgroupSize=32};
        VkComputePipelineCreateInfo invalid=pi;invalid.stage.pNext=&required;invalid.stage.flags=0;VkPipeline rejected=VK_NULL_HANDLE;
        assert(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&invalid,NULL,&rejected)==VK_ERROR_FEATURE_NOT_PRESENT);
    }OK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&pipelines[i]));}
    VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};VkDescriptorPool pool;OK(vkCreateDescriptorPool(device,&dp,NULL,&pool));
    VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};VkDescriptorSet set;OK(vkAllocateDescriptorSets(device,&sa,&set));
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool command_pool;OK(vkCreateCommandPool(device,&pc,NULL,&command_pool));VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;OK(vkAllocateCommandBuffers(device,&ca,&command));VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;OK(vkCreateFence(device,&fc,NULL,&fence));
    for(unsigned pass=0;pass<2;pass++){
        if(pass){OK(vkResetFences(device,1,&fence));OK(vkResetCommandBuffer(command,0));}
        VkDescriptorBufferInfo buffers[2]={{pass?pointer.buffer:in.buffer,0,VK_WHOLE_SIZE},{out.buffer,0,VK_WHOLE_SIZE}};
        VkWriteDescriptorSet writes[2]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=buffers},{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=buffers+1}};vkUpdateDescriptorSets(device,2,writes,0,NULL);
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(command,&begin));vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[pass]);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,8,&gpu_address);vkCmdDispatch(command,1,1,1);
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(vkEndCommandBuffer(command));
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};OK(vkQueueSubmit(queue,1,&submit,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ULL));
        if(!pass){
            for(unsigned i=0;i<64;i++){
                float wanted=0;for(unsigned k=0;k<8;k++)wanted+=(float)input->a[(i/8)*8+k]*(float)input->b[k*8+i%8];assert(output->matrix[i]==wanted);
                int32_t narrow=(int32_t)input->x[i]+2+(int32_t)input->y[i]*3;assert(output->narrow[i]==narrow);
                uint32_t lanes=output->ballots[i];assert(lanes>=sizes.minSubgroupSize && lanes<=sizes.maxSubgroupSize);assert(output->reductions[i]==lanes*(lanes+1)/2 && output->shuffles[i]==1);assert(address_data[i]==i*7+(uint32_t)(narrow+2048));
            }
            assert(output->total==64 && output->shared_total==64);puts("neural-main-ok: 64 FP16 GEMM values; INT8/INT16; FP16 subgroup reduce/shuffle/ballot; FP32 buffer atomic add; Vulkan memory model; descriptor-free GPU-address coherent read/write");
        }else{for(int32_t i=0;i<64;i++)assert((int32_t)address_data[i]==i*2-38);puts("neural-dot-ok: 64 exact packed signed INT8 dot products");}
    }
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,command_pool,NULL);vkDestroyDescriptorPool(device,pool,NULL);for(unsigned i=0;i<2;i++){vkDestroyPipeline(device,pipelines[i],NULL);vkDestroyShaderModule(device,modules[i],NULL);}vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);release(pointer);release(out);release(in);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);puts("neural-compute-ok");return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary Linux Vulkan application: native swapchain images are never copied
// to guest memory. The same program supports finite lifecycle or continuous UI tests.
#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define OK(call) do {VkResult r=(call);if(r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR){fprintf(stderr,"%s: %d at %d\n",#call,r,__LINE__);abort();}}while(0)
static VkShaderModule shader(VkDevice d,const char *path) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n%4==0);rewind(f);
    uint32_t *code=malloc((size_t)n);assert(code && fread(code,1,(size_t)n,f)==(size_t)n);fclose(f);
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)n,.pCode=code};VkShaderModule out;OK(vkCreateShaderModule(d,&info,NULL,&out));free(code);return out;
}

int main(int argc,char **argv) {
    assert(argc==3 || argc==4);setbuf(stdout,NULL);int continuous=argc==4;
    const char *extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_DISPLAY_EXTENSION_NAME};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
    VkInstance instance;OK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t n=1;VkPhysicalDevice physical;OK(vkEnumeratePhysicalDevices(instance,&n,&physical));assert(n==1);
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);printf("Vulkan present driver: %s\n",properties.deviceName);
    n=0;OK(vkGetPhysicalDeviceDisplayPropertiesKHR(physical,&n,NULL));assert(n==1);
    VkDisplayPropertiesKHR display;OK(vkGetPhysicalDeviceDisplayPropertiesKHR(physical,&n,&display));
    n=1;VkDisplayPlanePropertiesKHR plane;OK(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical,&n,&plane));assert(plane.currentDisplay==display.display);
    n=1;VkDisplayKHR supported;OK(vkGetDisplayPlaneSupportedDisplaysKHR(physical,0,&n,&supported));assert(supported==display.display);
    n=1;VkDisplayModePropertiesKHR mode;OK(vkGetDisplayModePropertiesKHR(physical,display.display,&n,&mode));
    VkDisplayPlaneCapabilitiesKHR planeCaps;OK(vkGetDisplayPlaneCapabilitiesKHR(physical,mode.displayMode,0,&planeCaps));assert(planeCaps.supportedAlpha&VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR);
    VkDisplayModeCreateInfoKHR modeInfo={.sType=VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR,.parameters=mode.parameters};VkDisplayModeKHR createdMode;OK(vkCreateDisplayModeKHR(physical,display.display,&modeInfo,NULL,&createdMode));assert(createdMode==mode.displayMode);
    VkDisplaySurfaceCreateInfoKHR si={.sType=VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,.displayMode=mode.displayMode,.transform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,.globalAlpha=1,.alphaMode=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,.imageExtent=mode.parameters.visibleRegion};
    VkSurfaceKHR surface;OK(vkCreateDisplayPlaneSurfaceKHR(instance,&si,NULL,&surface));
    VkSurfaceKHR duplicate;assert(vkCreateDisplayPlaneSurfaceKHR(instance,&si,NULL,&duplicate)==VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,NULL);VkQueueFamilyProperties *families=calloc(n,sizeof(*families));assert(families);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families);uint32_t family=0;while(family<n && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<n);free(families);
    VkBool32 support=VK_FALSE;OK(vkGetPhysicalDeviceSurfaceSupportKHR(physical,family,surface,&support));assert(support);
    VkSurfaceCapabilitiesKHR caps;OK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps));assert(caps.currentExtent.width==1024 && caps.currentExtent.height==768);
    n=1;VkSurfaceFormatKHR format;OK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&n,&format));assert(format.format==VK_FORMAT_B8G8R8A8_UNORM);
    n=1;VkPresentModeKHR presentMode;OK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&n,&presentMode));assert(presentMode==VK_PRESENT_MODE_FIFO_KHR);
    float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};const char *deviceExtension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=1,.ppEnabledExtensionNames=&deviceExtension};VkDevice device;OK(vkCreateDevice(physical,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkAttachmentDescription attachment={.format=format.format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};VkSubpassDependency dependency={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};VkRenderPassCreateInfo passInfo={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=1,.pDependencies=&dependency};VkRenderPass pass;OK(vkCreateRenderPass(device,&passInfo,NULL,&pass));
    VkPipelineLayoutCreateInfo layoutInfo={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VkPipelineLayout layout;OK(vkCreatePipelineLayout(device,&layoutInfo,NULL,&layout));
    VkShaderModule vert=shader(device,argv[1]),frag=shader(device,argv[2]);VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vert,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo multi={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineColorBlendAttachmentState color={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&color};VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamicStates};
    VkGraphicsPipelineCreateInfo pipelineInfo={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&multi,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=layout,.renderPass=pass,.basePipelineIndex=-1};VkPipeline pipeline;OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,NULL,&pipeline));
    vkDestroyShaderModule(device,vert,NULL);vkDestroyShaderModule(device,frag,NULL);

    VkCommandPoolCreateInfo poolInfo={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool pool;OK(vkCreateCommandPool(device,&poolInfo,NULL,&pool));VkCommandBufferAllocateInfo commandInfo={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;OK(vkAllocateCommandBuffers(device,&commandInfo,&command));VkFenceCreateInfo fenceInfo={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;OK(vkCreateFence(device,&fenceInfo,NULL,&fence));VkSemaphoreCreateInfo semInfo={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore acquired;OK(vkCreateSemaphore(device,&semInfo,NULL,&acquired));
    VkSwapchainKHR previous=VK_NULL_HANDLE;unsigned frames=0;
    for(unsigned round=0;round<2;round++) {
        VkSwapchainCreateInfoKHR chainInfo={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=surface,.minImageCount=caps.minImageCount,.imageFormat=format.format,.imageColorSpace=format.colorSpace,.imageExtent=caps.currentExtent,.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,.presentMode=presentMode,.clipped=VK_TRUE,.oldSwapchain=previous};
        VkSwapchainKHR chain;OK(vkCreateSwapchainKHR(device,&chainInfo,NULL,&chain));
        if(previous) {uint32_t index;assert(vkAcquireNextImageKHR(device,previous,0,acquired,VK_NULL_HANDLE,&index)==VK_ERROR_OUT_OF_DATE_KHR);vkDestroySwapchainKHR(device,previous,NULL);}
        uint32_t count=0;OK(vkGetSwapchainImagesKHR(device,chain,&count,NULL));assert(count>=caps.minImageCount && count<=4);VkImage images[4];VkImageView views[4];VkFramebuffer buffers[4];VkSemaphore finished[4];n=0;assert(vkGetSwapchainImagesKHR(device,chain,&n,images)==VK_INCOMPLETE);assert(n==0);n=count;OK(vkGetSwapchainImagesKHR(device,chain,&n,images));
        for(uint32_t i=0;i<count;i++) {
            VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};OK(vkCreateImageView(device,&vi,NULL,&views[i]));VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&views[i],.width=caps.currentExtent.width,.height=caps.currentExtent.height,.layers=1};OK(vkCreateFramebuffer(device,&fi,NULL,&buffers[i]));OK(vkCreateSemaphore(device,&semInfo,NULL,&finished[i]));
        }
        for(unsigned frame=0;frame<12 || (continuous && round==1);frame++) {
            uint32_t index;OK(vkAcquireNextImageKHR(device,chain,10000000000ULL,acquired,VK_NULL_HANDLE,&index));assert(index<count);
            OK(vkResetCommandBuffer(command,0));OK(vkResetFences(device,1,&fence));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(command,&begin));
            VkClearValue clear={.color={{8.0f/255,12.0f/255,20.0f/255,1}}};VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=buffers[index],.renderArea={{0,0},caps.currentExtent},.clearValueCount=1,.pClearValues=&clear};vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
            VkViewport viewport={0,0,(float)caps.currentExtent.width,(float)caps.currentExtent.height,0,1};vkCmdSetViewport(command,0,1,&viewport);VkRect2D scissor={{frame%60>=30?(int32_t)caps.currentExtent.width/2:0,0},{frame%60>=30?caps.currentExtent.width/2:caps.currentExtent.width,caps.currentExtent.height}};vkCmdSetScissor(command,0,1,&scissor);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);OK(vkEndCommandBuffer(command));
            VkPipelineStageFlags stage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&acquired,.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&command,.signalSemaphoreCount=1,.pSignalSemaphores=&finished[index]};OK(vkQueueSubmit(queue,1,&submit,fence));VkResult result;VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,.pWaitSemaphores=&finished[index],.swapchainCount=1,.pSwapchains=&chain,.pImageIndices=&index,.pResults=&result};OK(vkQueuePresentKHR(queue,&present));OK(result);OK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL));
            frames++;if(frame==0)printf("present round %u: %ux%u native swapchain active\n",round+1,caps.currentExtent.width,caps.currentExtent.height);
            struct timespec pause={0,16000000};nanosleep(&pause,NULL);
        }
        OK(vkDeviceWaitIdle(device));OK(vkResetCommandBuffer(command,0));
        for(uint32_t i=0;i<count;i++){vkDestroyFramebuffer(device,buffers[i],NULL);vkDestroyImageView(device,views[i],NULL);vkDestroySemaphore(device,finished[i],NULL);}
        previous=chain;
    }
    vkDestroySwapchainKHR(device,previous,NULL);vkDestroySemaphore(device,acquired,NULL);vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyDevice(device,NULL);vkDestroySurfaceKHR(instance,surface,NULL);
    // Releasing the producer permits a fresh standard surface on the same layer.
    OK(vkCreateDisplayPlaneSurfaceKHR(instance,&si,NULL,&surface));vkDestroySurfaceKHR(instance,surface,NULL);vkDestroyInstance(instance,NULL);printf("vulkan-present-ok frames=%u\n",frames);return 0;
}

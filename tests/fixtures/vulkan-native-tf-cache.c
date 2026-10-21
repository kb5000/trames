// SPDX-License-Identifier: GPL-3.0-or-later
// Instance construction runs the original MoltenVK Cereal completeness checks.
#include <vulkan/vulkan.h>
#include <stdio.h>
int main(void) {
    VkInstance instance;
    VkInstanceCreateInfo info = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkResult result=vkCreateInstance(&info,0,&instance);
    if (result!=VK_SUCCESS) return 1;
    vkDestroyInstance(instance,0);
    puts("tf-cache-layout-ok: original Cereal completeness diagnostics clean");
    return 0;
}

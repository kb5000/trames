// SPDX-License-Identifier: GPL-3.0-or-later
// Templates unpack ordinary guest descriptor data into the existing validated
// native writes. No guest pointer, allocation callback or template handle is
// sent to the host. Vulkan's original descriptor-set synchronization applies.
struct descriptor_template {
    DescriptorTemplate *next;
    uint32_t count;
    VkDescriptorUpdateTemplateEntry entries[];
};
static void free_templates(DescriptorTemplate *head) {
    while(head){DescriptorTemplate *next=head->next;free(head);head=next;}
}
static size_t descriptor_record_size(VkDescriptorType type) {
    switch(type){
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:return sizeof(VkDescriptorBufferInfo);
    case VK_DESCRIPTOR_TYPE_SAMPLER:case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:return sizeof(VkDescriptorImageInfo);
    default:return 0;
    }
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice device,const VkDescriptorUpdateTemplateCreateInfo *info,const VkAllocationCallbacks *allocator,VkDescriptorUpdateTemplate *out) {
    CHECK(info,VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO);ALLOCATOR(allocator);
    if(info->flags || info->templateType!=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET || info->descriptorUpdateEntryCount>64)return VK_ERROR_FEATURE_NOT_PRESENT;
    for(uint32_t i=0;i<info->descriptorUpdateEntryCount;i++){
        const VkDescriptorUpdateTemplateEntry *e=info->pDescriptorUpdateEntries+i;
        size_t size=descriptor_record_size(e->descriptorType);
        if(!size || !e->descriptorCount || e->descriptorCount>64 || e->offset>SIZE_MAX-size ||
           (e->descriptorCount>1 && e->stride>(SIZE_MAX-size-e->offset)/(e->descriptorCount-1)))return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    DescriptorTemplate *t=malloc(sizeof(*t)+info->descriptorUpdateEntryCount*sizeof(*t->entries));
    if(!t)return VK_ERROR_OUT_OF_HOST_MEMORY;
    t->count=info->descriptorUpdateEntryCount;
    if(t->count)memcpy(t->entries,info->pDescriptorUpdateEntries,t->count*sizeof(*t->entries));
    Dispatch *d=D(device);pthread_mutex_lock(&d->lock);t->next=d->templates;d->templates=t;pthread_mutex_unlock(&d->lock);
    *out=(VkDescriptorUpdateTemplate)(uintptr_t)t;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplateKHR(VkDevice d,const VkDescriptorUpdateTemplateCreateInfo *info,const VkAllocationCallbacks *allocator,VkDescriptorUpdateTemplate *out) {return vkCreateDescriptorUpdateTemplate(d,info,allocator,out);}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice device,VkDescriptorUpdateTemplate handle,const VkAllocationCallbacks *allocator) {
    if(!handle)return;if(allocator)checked(VK_ERROR_FEATURE_NOT_PRESENT);
    Dispatch *d=D(device);pthread_mutex_lock(&d->lock);
    for(DescriptorTemplate **p=&d->templates;*p;p=&(*p)->next){
        if((uintptr_t)*p==(uintptr_t)handle){DescriptorTemplate *t=*p;*p=t->next;free(t);break;}
    }
    pthread_mutex_unlock(&d->lock);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplateKHR(VkDevice d,VkDescriptorUpdateTemplate t,const VkAllocationCallbacks *allocator) {vkDestroyDescriptorUpdateTemplate(d,t,allocator);}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice device,VkDescriptorSet set,VkDescriptorUpdateTemplate handle,const void *data) {
    DescriptorTemplate *t=(DescriptorTemplate *)(uintptr_t)handle;
    // Template data is immutable. Destroying a template while updating with it
    // requires external synchronization in Vulkan; no new algorithm lock.
    for(uint32_t i=0;i<t->count;i++){
        const VkDescriptorUpdateTemplateEntry *e=t->entries+i;
        VkDescriptorBufferInfo buffers[64];VkDescriptorImageInfo images[64];
        const unsigned char *source=(const unsigned char *)data+e->offset;
        for(uint32_t j=0;j<e->descriptorCount;j++){
            const void *record=source+j*e->stride;
            if(e->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || e->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)memcpy(buffers+j,record,sizeof(*buffers));
            else memcpy(images+j,record,sizeof(*images));
        }
        VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=e->dstBinding,.dstArrayElement=e->dstArrayElement,
            .descriptorCount=e->descriptorCount,.descriptorType=e->descriptorType,.pBufferInfo=buffers,.pImageInfo=images};
        vkUpdateDescriptorSets(device,1,&write,0,NULL);
    }
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateKHR(VkDevice d,VkDescriptorSet set,VkDescriptorUpdateTemplate t,const void *data) {vkUpdateDescriptorSetWithTemplate(d,set,t,data);}

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise public Vulkan TF topology/index/restart/indirect and nonzero queries.

Derive from the already validated ordinary guest client without changing it.
Each captured scalar, query element, copy availability and padding is checked.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def source():
    text = (ROOT/'tests/fixtures/vulkan-feedback.c').read_text()
    substitutions = {
        'assert(argc==2);': 'assert(argc==5);unsigned topology=(unsigned)atoi(argv[2]),index_bits=(unsigned)atoi(argv[3]),indirect=(unsigned)atoi(argv[4]);',
        'buffer(0,VK_BUFFER_USAGE_TRANSFER_DST_BIT)': 'buffer(0,VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)',
        '.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST': '.topology=topology,.primitiveRestartEnable=(index_bits && (topology==2 || topology==4 || topology==5))',
        'sizes[]={144,48}': 'sizes[]={960,320}',
        'qc.queryType=VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;qc.queryCount=1': 'qc.queryType=VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;qc.queryCount=3',
        'vkCmdResetQueryPool(c,tf_pool,0,1)': 'vkCmdResetQueryPool(c,tf_pool,0,3)',
        'begin_query(c,tf_pool,0,0,0)': 'begin_query(c,tf_pool,1,0,0)',
        'end_query(c,tf_pool,0,0)': 'end_query(c,tf_pool,1,0)',
        'vkCmdCopyQueryPoolResults(c,tf_pool,0,1': 'vkCmdCopyQueryPoolResults(c,tf_pool,1,1',
        'vkGetQueryPoolResults(device,tf_pool,0,1': 'vkGetQueryPoolResults(device,tf_pool,1,1',
        'vkCmdDraw(c,3,2,2,4)':
          'if(index_bits) { vkCmdBindIndexBuffer(c,allocations[0].buffer,512,index_bits==16?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32); if(indirect)vkCmdDrawIndexedIndirect(c,allocations[0].buffer,640,1,20);else vkCmdDrawIndexed(c,9,2,1,1,4); } else if(indirect)vkCmdDrawIndirect(c,allocations[0].buffer,640,1,16);else vkCmdDraw(c,5,2,2,4)',
    }
    for old,new in substitutions.items():
        assert text.count(old)==1,(old,text.count(old))
        text=text.replace(old,new)
    at=text.index('    VkQueue queue;')
    text=text[:at]+r'''    if(feedback){
        uint32_t indices[]={0,2,3,4,5,0xffffffffu,3,4,5,6};
        if(index_bits==16){uint16_t short_indices[10];for(unsigned i=0;i<10;i++)short_indices[i]=(uint16_t)indices[i];memcpy(allocations[0].data+512,short_indices,20);}
        else memcpy(allocations[0].data+512,indices,40);
        uint32_t args[]={index_bits?9:5,2,index_bits?1:2,index_bits?1:4,4};memcpy(allocations[0].data+640,args,sizeof(args));
    }
'''+text[at:]
    begin=text.index('assert(captured[0]==6 && captured[1]==6 && captured[2]);')
    end=text.index('        puts("guest-feedback-ok:',begin)
    text=text[:begin]+r'''assert(captured[0]==captured[1] && captured[2]);assert(!memcmp(allocations[0].data+128,captured,sizeof(captured)));
        unsigned map[40],used=0;unsigned input[]={2,3,4,5,0xffffffffu,3,4,5,6};
        unsigned pending[3]={0},n=0,parity=0,size=topology==0?1:topology<=2?2:3;
        for(unsigned v=0;v<(index_bits?9u:5u);v++){
            unsigned index=index_bits?input[v]:v+2;
            if(index==0xffffffffu){n=0;parity=0;continue;}
            pending[n++]=index+(index_bits?1:0);if(n<size)continue;
            for(unsigned k=0;k<size;k++)map[used++]=pending[topology==4 && parity && k>0?3-k:topology==5?(k+1)%3:k];
            if(topology==2){pending[0]=pending[1];n=1;}
            else if(topology==4){pending[0]=pending[1];pending[1]=pending[2];n=2;parity^=1;}
            else if(topology==5){pending[1]=pending[2];n=2;}
            else n=0;
        }
        assert(captured[0]==2*used/size);
        for(unsigned i=0;i<used*2;i++){
            float got[4],expected[]={(float)map[i%used],4+(float)(i/used),map[i%used]*3+1,17};
            memcpy(got,allocations[1].data+16+i*24+4,16);assert(!memcmp(got,expected,16));
            float other;memcpy(&other,allocations[2].data+16+i*8,4);assert(other==map[i%used]+100);
            for(unsigned j=0;j<4;j++)assert(allocations[1].data[16+i*24+j]==0xa5);
        }
        printf("tf-runtime-ok: topology=%u index=%u indirect=%u primitives=%llu scalars=%u\n",topology,index_bits,indirect,(unsigned long long)captured[0],used*10);
'''+text[end:]
    return text



def raster_source():
    text=(ROOT/'tests/fixtures/vulkan-feedback.c').read_text()
    replacements={
      'assert(argc==2);':'assert(argc==3);',
      '.ppEnabledExtensionNames=&extension};OK(vkCreateDevice':'.ppEnabledExtensionNames=&extension,.pEnabledFeatures=&features.features};features.features.vertexPipelineStoresAndAtomics=VK_TRUE;OK(vkCreateDevice',
      'buffer(0,VK_BUFFER_USAGE_TRANSFER_DST_BIT)':'buffer(0,VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)',
      'VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};':
      'VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&reference};',
      '.subpassCount=1,.pSubpasses=&sub}':'.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&sub}',
      '.renderPass=pass,.width=8':'.renderPass=pass,.attachmentCount=1,.pAttachments=&view,.width=8',
      'VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};':r"""VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT};
        VkDescriptorSetLayoutCreateInfo dl={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};OK(vkCreateDescriptorSetLayout(device,&dl,NULL,&descriptors));
        VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};OK(vkCreateDescriptorPool(device,&dpi,NULL,&descriptor_pool));
        VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&descriptors};OK(vkAllocateDescriptorSets(device,&da,&descriptor_set));
        VkDescriptorBufferInfo db={allocations[0].buffer,64,16};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptor_set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&db};vkUpdateDescriptorSets(device,1,&write,0,NULL);
        VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&descriptors};""",
      'VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};':'VkVertexInputBindingDescription vb={0,16,VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription va={0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0};VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&vb,.vertexAttributeDescriptionCount=1,.pVertexAttributeDescriptions=&va};',
      '.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST':'.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST',
      '.rasterizerDiscardEnable=VK_TRUE':'.rasterizerDiscardEnable=VK_FALSE',
      'VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};':'VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};',
      '.stageCount=1,.pStages=&stage':'.stageCount=2,.pStages=stages',
      '.renderArea={{0,0},{8,8}}}':'.renderArea={{0,0},{8,8}},.clearValueCount=1,.pClearValues=&clear}',
      'vkCmdDraw(c,3,2,2,4)':'vkCmdDraw(c,3,1,2,4)',
      'assert(occlusion[0]==0 && occlusion[1])':'assert(occlusion[0]>0 && occlusion[1])',
      'assert(gpu_occlusion[0]==0 && gpu_occlusion[1])':'assert(gpu_occlusion[0]>0 && gpu_occlusion[1])',
      'assert(captured[0]==6 && captured[1]==6 && captured[2])':'assert(captured[0]==1 && captured[1]==1 && captured[2])',
      'i<6;i++':'i<3;i++',
    }
    for old,new in replacements.items():
        assert text.count(old)==1,(old,text.count(old))
        text=text.replace(old,new)
    marker='    VkQueryPool time_pool='
    image=r"""    VkImage image;VkDeviceMemory image_memory;VkImageView view;
    VkDescriptorSetLayout descriptors;VkDescriptorPool descriptor_pool;VkDescriptorSet descriptor_set;VkShaderModule fragment;
    VkImageCreateInfo image_info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={8,8,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};OK(vkCreateImage(device,&image_info,NULL,&image));
    VkMemoryRequirements requirement;vkGetImageMemoryRequirements(device,image,&requirement);unsigned memory_type=0;while(!(requirement.memoryTypeBits&(1u<<memory_type)))memory_type++;
    VkMemoryAllocateInfo memory_info={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirement.size,.memoryTypeIndex=memory_type};OK(vkAllocateMemory(device,&memory_info,NULL,&image_memory));OK(vkBindImageMemory(device,image,image_memory,0));
    VkImageViewCreateInfo view_info={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};OK(vkCreateImageView(device,&view_info,NULL,&view));
    memset(allocations[0].data+64,0,16);float positions[][4]={{0},{0},{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}};memcpy(allocations[0].data+768,positions,sizeof(positions));
"""
    text=text.replace(marker,image+marker)
    marker='        VkPipelineVertexInputStateCreateInfo vi='
    # vi now follows VBO descriptions, so place fragment code before stage declaration.
    marker='        VkPipelineShaderStageCreateInfo stage='
    frag=r"""        FILE *fragment_file=fopen(argv[2],"rb");assert(fragment_file);fseek(fragment_file,0,SEEK_END);long fragment_size=ftell(fragment_file);rewind(fragment_file);uint32_t *fragment_code=malloc(fragment_size);assert(fragment_code && fread(fragment_code,1,fragment_size,fragment_file)==(size_t)fragment_size);fclose(fragment_file);
        VkShaderModuleCreateInfo fragment_info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=fragment_size,.pCode=fragment_code};OK(vkCreateShaderModule(device,&fragment_info,NULL,&fragment));free(fragment_code);
"""
    text=text.replace(marker,frag+marker)
    text=text.replace('        VkVertexInputBindingDescription vb=', '        VkPipelineShaderStageCreateInfo stages[]={stage,{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};\n        VkVertexInputBindingDescription vb=')
    text=text.replace('    VkRenderPassBeginInfo pb=', '    VkClearValue clear={.color={{0,0,0,0}}};VkRenderPassBeginInfo pb=')
    text=text.replace('        vkCmdBindPipeline(c,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);', '        vkCmdBindPipeline(c,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);VkDeviceSize vertex_offset=768;vkCmdBindVertexBuffers(c,0,1,&allocations[0].buffer,&vertex_offset);vkCmdBindDescriptorSets(c,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&descriptor_set,0,NULL);')
    text=text.replace('    if(feedback)vkCmdCopyQueryPoolResults', '    VkMemoryBarrier image_barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&image_barrier,0,NULL,0,NULL);VkBufferImageCopy image_copy={.bufferOffset=512,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={8,8,1}};vkCmdCopyImageToBuffer(c,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,allocations[0].buffer,1,&image_copy);\n    if(feedback)vkCmdCopyQueryPoolResults')
    text=text.replace('    vkDestroyCommandPool(device,pool,NULL);', '    uint32_t calls;memcpy(&calls,allocations[0].data+64,4);assert(calls==3);for(unsigned i=0;i<64;i++){const unsigned char expected[]={64,128,191,255};assert(!memcmp(allocations[0].data+512+i*4,expected,4));}puts("tf-raster-ok: original VBO and SSBO descriptor; exactly 3 VS calls; 64 exact GPU RGBA pixels after capture/replay");\n    vkDestroyShaderModule(device,fragment,NULL);vkDestroyDescriptorPool(device,descriptor_pool,NULL);vkDestroyDescriptorSetLayout(device,descriptors,NULL);\n    vkDestroyCommandPool(device,pool,NULL);')
    text=text.replace('    for(unsigned i=0;i<(feedback?', '    vkDestroyImageView(device,view,NULL);vkDestroyImage(device,image,NULL);vkFreeMemory(device,image_memory,NULL);\n    for(unsigned i=0;i<(feedback?')
    return text

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'target/vulkan-tf-runtime')
    p.add_argument('--include',type=Path,required=True)
    p.add_argument('--shader',type=Path,required=True)
    p.add_argument('--library',required=True)
    p.add_argument('--glslang',default='glslangValidator')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    c=out/'public-tf.c';c.write_text(source())
    obj=out/'public-tf.o';binary=out/'public-tf'
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(a.include.resolve()),'-c',str(c),'-o',str(obj)],check=True)
    link=[os.environ.get('CXX','c++'),str(obj),a.library,'-o',str(binary)]
    if platform.system()=='Darwin':
        for framework in ('Metal','Foundation','IOKit','IOSurface','QuartzCore','AppKit','CoreGraphics'):
            link+=['-framework',framework]
    subprocess.run(link,check=True)
    def execute(command,name):
        run=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        (out/(name+'.log')).write_text(run.stdout)
        if run.returncode:
            print(run.stdout,flush=True)
            raise RuntimeError(f'{name}: exit {run.returncode}')
        return run
    cases=[]
    for topology in range(6):
        for bits in (0,16,32):
            if bits and topology not in (2,4,5):continue
            for indirect in (0,1):
                command=[str(binary),str(a.shader.resolve()),str(topology),str(bits),str(indirect)]
                run=execute(command,f'topology-{topology}-index-{bits}-indirect-{indirect}')
                line=next(s for s in run.stdout.splitlines() if s.startswith('tf-runtime-ok:'))
                print(line,flush=True);cases.append(dict(topology=topology,index_bits=bits,indirect=bool(indirect),result=line))
    shaders=[]
    for stage in ('vert','frag'):
        shader=out/('raster.'+stage+'.spv')
        subprocess.run([a.glslang,'-V','--target-env','vulkan1.0',str(ROOT/('tests/fixtures/vulkan-native-tf-runtime.'+stage)),'-o',str(shader)],check=True)
        shaders.append(str(shader))
    raster=out/'public-tf-raster.c';raster.write_text(raster_source())
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(a.include.resolve()),'-c',str(raster),'-o',str(obj)],check=True)
    subprocess.run(link,check=True)
    run=execute([str(binary),*shaders],'raster')
    raster_result=next(s for s in run.stdout.splitlines() if s.startswith('tf-raster-ok:'));print(raster_result,flush=True)
    # Real VBO fetch, restart filtering and shader side effects across signed
    # baseVertex offsets; all-marker draws must execute no VS at all.
    restart_results=[]
    for bits in (16,32):
        for base in (-1,1,None):
            restart=raster_source().replace('VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT','VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT')
            restart=restart.replace('.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST','.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,.primitiveRestartEnable=VK_TRUE')
            indices=[2-base,3-base,4-base,0xffffffff,2-base,3-base,4-base] if base is not None else [0xffffffff]*7
            if bits==16:indices=[x&0xffff for x in indices]
            values=','.join(str(x)+'u' for x in indices)
            extra='uint'+str(bits)+'_t indices[]={'+values+'};memcpy(allocations[0].data+880,indices,sizeof(indices));'
            restart=restart.replace('memcpy(allocations[0].data+768,positions,sizeof(positions));','memcpy(allocations[0].data+768,positions,sizeof(positions));'+extra)
            restart=restart.replace('vkCmdDraw(c,3,1,2,4)','vkCmdBindIndexBuffer(c,allocations[0].buffer,880,VK_INDEX_TYPE_UINT'+str(bits)+');vkCmdDrawIndexed(c,7,1,0,'+str(base or 0)+',4)')
            restart=restart.replace('captured[0]==1 && captured[1]==1','captured[0]==2 && captured[1]==2').replace('i<3;i++','i<6;i++').replace('4+i/3','4').replace('calls==3','calls==6').replace('exactly 3 VS calls','exactly 6 VS calls; indexed restart VBO')
            if base is None:
                restart=restart.replace('captured[0]==2 && captured[1]==2','captured[0]==0 && captured[1]==0').replace('i<6;i++','i<0;i++').replace('calls==6','calls==0').replace('occlusion[0]>0','occlusion[0]==0').replace('gpu_occlusion[0]>0','gpu_occlusion[0]==0').replace('expected[]={64,128,191,255}','expected[]={0,0,0,0}').replace('exactly 6 VS calls','exactly 0 VS calls; all markers')
                # Avoid a constant-false unsigned comparison warning.
                restart=restart.replace('for(unsigned i=0;i<0;i++){','for(unsigned i=0;i<(unsigned)captured[0];i++){')
            raster.write_text(restart)
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(a.include.resolve()),'-c',str(raster),'-o',str(obj)],check=True)
            subprocess.run(link,check=True)
            name=f'restart-raster-u{bits}-base-{base}'
            run=execute([str(binary),*shaders],name)
            line=next(s for s in run.stdout.splitlines() if s.startswith('tf-raster-ok:'));print(name+': '+line,flush=True)
            restart_results.append(dict(index_bits=bits,base_vertex=base,result=line))

    bounds_result=None
    if platform.system()=='Darwin':
        # Exceed the documented GPU indirect scratch bound. Dropping the draw
        # and reporting success would be a driver bug; fence wait must report
        # the asynchronous allocation/device failure.
        bounds=source().replace('index_bits?9:5,2,index_bits?1:2','200000,2,index_bits?1:2')
        old='OK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));'
        assert bounds.count(old)==1
        bounds=bounds.replace(old,'VkResult result=vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX);assert(result==VK_ERROR_DEVICE_LOST);puts("tf-bounds-error-ok: oversized GPU indirect draw reports device failure without CPU count readback");vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);return 0;')
        c.write_text(bounds)
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(a.include.resolve()),'-c',str(c),'-o',str(obj)],check=True)
        subprocess.run(link,check=True)
        run=execute([str(binary),str(a.shader.resolve()),'0','0','1'],'bounds-error')
        bounds_result=next(s for s in run.stdout.splitlines() if s.startswith('tf-bounds-error-ok:'));print(bounds_result,flush=True)
    result=dict(bounds_error=bounds_result,raster=raster_result,restart_raster=restart_results,cases=cases,source_sha256=hashlib.sha256(c.read_bytes()).hexdigest(),shader_sha256=hashlib.sha256(a.shader.read_bytes()).hexdigest())
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':main()

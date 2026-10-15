// SPDX-License-Identifier: GPL-3.0-or-later
//! Selected core 1.0 offscreen graphics. No guest Vulkan structure/pointer is
//! handed to the driver; all arrays are decoded to scoped native records.
use super::*;

#[derive(Clone, Copy)]
pub(super) struct ImageShape {
    width: u32,
    height: u32,
    format: vk::Format,
}
#[derive(Clone, Copy)]
pub(super) struct PassInfo {
    format: vk::Format,
    clear: bool,
}
#[derive(Default)]
pub(super) struct Recording {
    pub pass: Option<Rc<Object>>,
    pub pipeline: Option<Rc<Object>>,
    viewport: bool,
    scissor: bool,
}
const DIMENSION: u32 = 4096;
fn format(raw: u64) -> Result<vk::Format> {
    match raw {
        37 => Ok(vk::Format::R8G8B8A8_UNORM),
        44 => Ok(vk::Format::B8G8R8A8_UNORM),
        _ => Err(vk::Result::ERROR_FORMAT_NOT_SUPPORTED.into()),
    }
}
fn usage(raw: u64) -> Result<vk::ImageUsageFlags> {
    let allowed = vk::ImageUsageFlags::COLOR_ATTACHMENT
        | vk::ImageUsageFlags::TRANSFER_SRC
        | vk::ImageUsageFlags::TRANSFER_DST;
    if raw == 0 || raw & !(allowed.as_raw() as u64) != 0 {
        return unsupported();
    }
    Ok(vk::ImageUsageFlags::from_raw(raw as u32))
}
fn layout(raw: u64, initial: bool) -> Result<vk::ImageLayout> {
    match raw {
        0 if initial => Ok(vk::ImageLayout::UNDEFINED),
        1 => Ok(vk::ImageLayout::GENERAL),
        2 => Ok(vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL),
        6 => Ok(vk::ImageLayout::TRANSFER_SRC_OPTIMAL),
        7 => Ok(vk::ImageLayout::TRANSFER_DST_OPTIMAL),
        _ => unsupported(),
    }
}
fn color_range() -> vk::ImageSubresourceRange {
    vk::ImageSubresourceRange::default()
        .aspect_mask(vk::ImageAspectFlags::COLOR)
        .level_count(1)
        .layer_count(1)
}
fn shape(object: &Object) -> Result<ImageShape> {
    object
        .image
        .get()
        .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))
}
impl Backend {
    // New native resources are owned before the final fallible guest write.
    // A backing I/O failure after preflight must not leak an unpublished object.
    fn publish(&mut self, m: &mut Memory, output: u64, id: u64) -> Result<()> {
        if let Err(error) = write(m, output, &id.to_le_bytes()) {
            self.objects.remove(&id);
            return Err(error);
        }
        Ok(())
    }
    pub(super) fn format_query(&self, m: &mut Memory, op: u64, a: [u64; 16]) -> Result<()> {
        let instance = &self.instance.as_ref().unwrap().native;
        if op == 58 {
            let mut bytes = vec![0u8; 12];
            if let Ok(f) = format(a[1]) {
                let p = unsafe { instance.get_physical_device_format_properties(self.physical, f) };
                let flags = p.optimal_tiling_features
                    & (vk::FormatFeatureFlags::COLOR_ATTACHMENT
                        | vk::FormatFeatureFlags::TRANSFER_SRC
                        | vk::FormatFeatureFlags::TRANSFER_DST);
                bytes[4..8].copy_from_slice(&flags.as_raw().to_le_bytes());
            }
            return write(m, a[2], &bytes);
        }
        output(m, a[6], 32)?;
        if a[2] != vk::ImageType::TYPE_2D.as_raw() as u64
            || a[3] != vk::ImageTiling::OPTIMAL.as_raw() as u64
            || a[5] != 0
        {
            return Err(vk::Result::ERROR_FORMAT_NOT_SUPPORTED.into());
        }
        let p = unsafe {
            instance.get_physical_device_image_format_properties(
                self.physical,
                format(a[1])?,
                vk::ImageType::TYPE_2D,
                vk::ImageTiling::OPTIMAL,
                usage(a[4])?,
                vk::ImageCreateFlags::empty(),
            )
        }?;
        let mut bytes = Vec::new();
        for n in [
            p.max_extent.width.min(DIMENSION),
            p.max_extent.height.min(DIMENSION),
            1,
            1,
            1,
            1,
        ] {
            bytes.extend(n.to_le_bytes());
        }
        bytes.extend(p.max_resource_size.min(MAX_ALLOCATION).to_le_bytes());
        write(m, a[6], &bytes)
    }
    pub(super) fn start_graphics_compile(&mut self, m: &Memory, a: [u64; 16]) -> Result<u64> {
        let device = self.device(a[0])?;
        let queues = unsafe {
            device
                .instance
                .native
                .get_physical_device_queue_family_properties(self.physical)
        };
        if !queues[self.family as usize]
            .queue_flags
            .contains(vk::QueueFlags::GRAPHICS)
        {
            return unsupported();
        }
        self.room(1)?;
        output(m, a[7], 8)?;
        let pipeline_layout = self.object(a[1], Kind::PipelineLayout)?;
        // First profile has generated vertices and no graphics descriptors or
        // push constants. Reject rather than silently ignore those layouts.
        if !pipeline_layout.deps.borrow().is_empty()
            || !pipeline_layout.push_ranges.borrow().is_empty()
        {
            return unsupported();
        }
        let vertex = self.object(a[2], Kind::Shader)?;
        let fragment = self.object(a[3], Kind::Shader)?;
        let name = |p| {
            std::ffi::CString::new(m.read_c_string(p, 256)?)
                .map_err(|_| Failure::Vk(vk::Result::ERROR_UNKNOWN))
        };
        let vertex_name = name(a[4])?;
        let fragment_name = name(a[5])?;
        let state = words64(m, a[6], 5)?;
        let pass = self.object(state[0], Kind::RenderPass)?;
        if !matches!(state[1], 3 | 4) || state[2] & !3 != 0 || state[3] > 1 || state[4] & !15 != 0 {
            return unsupported();
        }
        let (raw_layout, raw_vertex, raw_fragment, raw_pass) = (
            vk::PipelineLayout::from_raw(pipeline_layout.raw),
            vk::ShaderModule::from_raw(vertex.raw),
            vk::ShaderModule::from_raw(fragment.raw),
            vk::RenderPass::from_raw(pass.raw),
        );
        let permit = self
            .budget
            .compile()
            .ok_or(Failure::Vk(vk::Result::ERROR_OUT_OF_HOST_MEMORY))?;
        let task = crate::task::Task::spawn(move || {
            let _permit = permit;
            let stages = [
                vk::PipelineShaderStageCreateInfo::default()
                    .stage(vk::ShaderStageFlags::VERTEX)
                    .module(raw_vertex)
                    .name(&vertex_name),
                vk::PipelineShaderStageCreateInfo::default()
                    .stage(vk::ShaderStageFlags::FRAGMENT)
                    .module(raw_fragment)
                    .name(&fragment_name),
            ];
            let vertex = vk::PipelineVertexInputStateCreateInfo::default();
            let assembly = vk::PipelineInputAssemblyStateCreateInfo::default()
                .topology(vk::PrimitiveTopology::from_raw(state[1] as i32));
            let viewport = vk::PipelineViewportStateCreateInfo::default()
                .viewport_count(1)
                .scissor_count(1);
            let raster = vk::PipelineRasterizationStateCreateInfo::default()
                .polygon_mode(vk::PolygonMode::FILL)
                .cull_mode(vk::CullModeFlags::from_raw(state[2] as u32))
                .front_face(vk::FrontFace::from_raw(state[3] as i32))
                .line_width(1.0);
            let multisample = vk::PipelineMultisampleStateCreateInfo::default()
                .rasterization_samples(vk::SampleCountFlags::TYPE_1);
            let attachments = [vk::PipelineColorBlendAttachmentState::default()
                .color_write_mask(vk::ColorComponentFlags::from_raw(state[4] as u32))];
            let blend = vk::PipelineColorBlendStateCreateInfo::default().attachments(&attachments);
            let dynamic_states = [vk::DynamicState::VIEWPORT, vk::DynamicState::SCISSOR];
            let dynamic =
                vk::PipelineDynamicStateCreateInfo::default().dynamic_states(&dynamic_states);
            let info = [vk::GraphicsPipelineCreateInfo::default()
                .stages(&stages)
                .vertex_input_state(&vertex)
                .input_assembly_state(&assembly)
                .viewport_state(&viewport)
                .rasterization_state(&raster)
                .multisample_state(&multisample)
                .color_blend_state(&blend)
                .dynamic_state(&dynamic)
                .layout(raw_layout)
                .render_pass(raw_pass)
                .subpass(0)];
            match unsafe {
                device
                    .native
                    .create_graphics_pipelines(vk::PipelineCache::null(), &info, None)
            } {
                Ok(p) => Ok(CompiledPipeline { device, raw: p[0] }),
                Err((pipelines, error)) => {
                    for p in pipelines {
                        unsafe {
                            device.native.destroy_pipeline(p, None);
                        }
                    }
                    Err(error)
                }
            }
        })
        .map_err(|_| Failure::Vk(vk::Result::ERROR_OUT_OF_HOST_MEMORY))?;
        let job = self.id();
        self.compiles.insert(
            job,
            Compile {
                task,
                dependencies: vec![pipeline_layout, pass, vertex, fragment],
                bind_point: vk::PipelineBindPoint::GRAPHICS,
                output: a[7],
                abandoned: false,
            },
        );
        Ok(job)
    }
    pub(super) fn image_barriers(
        &self,
        m: &Memory,
        command: &Rc<Object>,
        address: u64,
        count: u64,
    ) -> Result<Vec<vk::ImageMemoryBarrier<'static>>> {
        let wire = words64(m, address, bounded(count, 64)? * 5)?;
        let mut barriers = Vec::new();
        let mut dependencies = Vec::new();
        for b in wire.chunks_exact(5) {
            let image = self.object(b[0], Kind::Image)?;
            if image.deps.borrow().is_empty() {
                return invalid();
            }
            barriers.push(
                vk::ImageMemoryBarrier::default()
                    .image(vk::Image::from_raw(image.raw))
                    .src_access_mask(vk::AccessFlags::from_raw(b[1] as u32))
                    .dst_access_mask(vk::AccessFlags::from_raw(b[2] as u32))
                    .old_layout(layout(b[3], true)?)
                    .new_layout(layout(b[4], false)?)
                    .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                    .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                    .subresource_range(color_range()),
            );
            dependencies.push(image);
        }
        for image in dependencies {
            command.retain(image);
        }
        Ok(barriers)
    }
    pub(super) fn graphics_call(&mut self, m: &mut Memory, op: u64, a: [u64; 16]) -> Result<()> {
        let device = self.device.as_ref().unwrap().clone();
        let d = &device.native;
        match op {
            60 => {
                self.room(1)?;
                output(m, a[5], 8)?;
                let f = format(a[1])?;
                let u = usage(a[4])?;
                if a[2] == 0 || a[3] == 0 || a[2] > DIMENSION as u64 || a[3] > DIMENSION as u64 {
                    return invalid();
                }
                let info = vk::ImageCreateInfo::default()
                    .image_type(vk::ImageType::TYPE_2D)
                    .format(f)
                    .extent(vk::Extent3D {
                        width: a[2] as u32,
                        height: a[3] as u32,
                        depth: 1,
                    })
                    .mip_levels(1)
                    .array_layers(1)
                    .samples(vk::SampleCountFlags::TYPE_1)
                    .tiling(vk::ImageTiling::OPTIMAL)
                    .usage(u)
                    .sharing_mode(vk::SharingMode::EXCLUSIVE)
                    .initial_layout(vk::ImageLayout::UNDEFINED);
                let raw = unsafe { d.create_image(&info, None) }?;
                let id = self.put(Kind::Image, raw.as_raw(), vec![], 0, u.as_raw());
                self.objects[&id].image.set(Some(ImageShape {
                    width: a[2] as u32,
                    height: a[3] as u32,
                    format: f,
                }));
                self.publish(m, a[5], id)?;
            }
            61 => self.remove(a[1], Kind::Image)?,
            62 => {
                let image = self.object(a[1], Kind::Image)?;
                let r = unsafe { d.get_image_memory_requirements(vk::Image::from_raw(image.raw)) };
                let mut bytes = Vec::new();
                bytes.extend(r.size.to_le_bytes());
                bytes.extend(r.alignment.to_le_bytes());
                bytes.extend(r.memory_type_bits.to_le_bytes());
                bytes.extend([0; 4]);
                write(m, a[2], &bytes)?;
            }
            63 => {
                let image = self.object(a[1], Kind::Image)?;
                let memory = self.object(a[2], Kind::Memory)?;
                let r = unsafe { d.get_image_memory_requirements(vk::Image::from_raw(image.raw)) };
                if !image.deps.borrow().is_empty()
                    || !a[3].is_multiple_of(r.alignment)
                    || r.memory_type_bits & (1u32 << memory.state.get()) == 0
                    || a[3].checked_add(r.size).is_none_or(|n| n > memory.size)
                {
                    return invalid();
                }
                unsafe {
                    d.bind_image_memory(
                        vk::Image::from_raw(image.raw),
                        vk::DeviceMemory::from_raw(memory.raw),
                        a[3],
                    )
                }?;
                image.retain(memory);
            }
            64 => {
                self.room(1)?;
                output(m, a[3], 8)?;
                let image = self.object(a[1], Kind::Image)?;
                let shape = shape(&image)?;
                if format(a[2])? != shape.format || image.deps.borrow().is_empty() {
                    return invalid();
                }
                let info = vk::ImageViewCreateInfo::default()
                    .image(vk::Image::from_raw(image.raw))
                    .view_type(vk::ImageViewType::TYPE_2D)
                    .format(shape.format)
                    .subresource_range(color_range());
                let raw = unsafe { d.create_image_view(&info, None) }?;
                let id = self.put(Kind::ImageView, raw.as_raw(), vec![image], 0, 0);
                self.objects[&id].image.set(Some(shape));
                self.publish(m, a[3], id)?;
            }
            65 => self.remove(a[1], Kind::ImageView)?,
            66 => {
                self.room(1)?;
                output(m, a[8], 8)?;
                let f = format(a[1])?;
                if a[2] > 2 || a[3] > 1 {
                    return unsupported();
                }
                let attachments = [vk::AttachmentDescription::default()
                    .format(f)
                    .samples(vk::SampleCountFlags::TYPE_1)
                    .load_op(vk::AttachmentLoadOp::from_raw(a[2] as i32))
                    .store_op(vk::AttachmentStoreOp::from_raw(a[3] as i32))
                    .stencil_load_op(vk::AttachmentLoadOp::DONT_CARE)
                    .stencil_store_op(vk::AttachmentStoreOp::DONT_CARE)
                    .initial_layout(layout(a[4], true)?)
                    .final_layout(layout(a[5], false)?)];
                let colors = [vk::AttachmentReference::default()
                    .attachment(0)
                    .layout(vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL)];
                let subpasses = [vk::SubpassDescription::default()
                    .pipeline_bind_point(vk::PipelineBindPoint::GRAPHICS)
                    .color_attachments(&colors)];
                let wire = words64(m, a[6], bounded(a[7], 8)? * 7)?;
                let mut dependencies = Vec::new();
                for x in wire.chunks_exact(7) {
                    if !matches!(x[0], 0 | 0xffffffff)
                        || !matches!(x[1], 0 | 0xffffffff)
                        || x[6] & !1 != 0
                    {
                        return unsupported();
                    }
                    dependencies.push(
                        vk::SubpassDependency::default()
                            .src_subpass(x[0] as u32)
                            .dst_subpass(x[1] as u32)
                            .src_stage_mask(compute_stage(x[2] as u32)?)
                            .dst_stage_mask(compute_stage(x[3] as u32)?)
                            .src_access_mask(vk::AccessFlags::from_raw(x[4] as u32))
                            .dst_access_mask(vk::AccessFlags::from_raw(x[5] as u32))
                            .dependency_flags(vk::DependencyFlags::from_raw(x[6] as u32)),
                    );
                }
                let info = vk::RenderPassCreateInfo::default()
                    .attachments(&attachments)
                    .subpasses(&subpasses)
                    .dependencies(&dependencies);
                let raw = unsafe { d.create_render_pass(&info, None) }?;
                let id = self.put(Kind::RenderPass, raw.as_raw(), vec![], 0, 0);
                self.objects[&id].pass.set(Some(PassInfo {
                    format: f,
                    clear: a[2] == 1,
                }));
                self.publish(m, a[8], id)?;
            }
            67 => self.remove(a[1], Kind::RenderPass)?,
            68 => {
                self.room(1)?;
                output(m, a[5], 8)?;
                let pass = self.object(a[1], Kind::RenderPass)?;
                let view = self.object(a[2], Kind::ImageView)?;
                let s = shape(&view)?;
                if pass.pass.get().unwrap().format != s.format
                    || a[3] == 0
                    || a[4] == 0
                    || a[3] > s.width as u64
                    || a[4] > s.height as u64
                    || view.deps.borrow()[0].flags & vk::ImageUsageFlags::COLOR_ATTACHMENT.as_raw()
                        == 0
                {
                    return invalid();
                }
                let attachments = [vk::ImageView::from_raw(view.raw)];
                let info = vk::FramebufferCreateInfo::default()
                    .render_pass(vk::RenderPass::from_raw(pass.raw))
                    .attachments(&attachments)
                    .width(a[3] as u32)
                    .height(a[4] as u32)
                    .layers(1);
                let raw = unsafe { d.create_framebuffer(&info, None) }?;
                let id = self.put(Kind::Framebuffer, raw.as_raw(), vec![pass, view], 0, 0);
                self.objects[&id].image.set(Some(ImageShape {
                    width: a[3] as u32,
                    height: a[4] as u32,
                    format: s.format,
                }));
                self.publish(m, a[5], id)?;
            }
            69 => self.remove(a[1], Kind::Framebuffer)?,
            71..=76 => {
                let c = self.object(a[1], Kind::CommandBuffer)?;
                if c.state.get() != 1 {
                    return invalid();
                }
                let raw = vk::CommandBuffer::from_raw(c.raw);
                match op {
                    71 => {
                        if c.recording.borrow().pass.is_some() {
                            return invalid();
                        }
                        let pass = self.object(a[2], Kind::RenderPass)?;
                        let fb = self.object(a[3], Kind::Framebuffer)?;
                        let s = shape(&fb)?;
                        if !Rc::ptr_eq(&fb.deps.borrow()[0], &pass)
                            || a[6] == 0
                            || a[7] == 0
                            || a[4].checked_add(a[6]).is_none_or(|x| x > s.width as u64)
                            || a[5].checked_add(a[7]).is_none_or(|y| y > s.height as u64)
                            || a[9] > 1
                            || pass.pass.get().unwrap().clear && a[9] != 1
                        {
                            return invalid();
                        }
                        let mut clears = Vec::new();
                        if a[9] != 0 {
                            let words = words32(m, a[8], 4)?;
                            clears.push(vk::ClearValue {
                                color: vk::ClearColorValue {
                                    float32: std::array::from_fn(|i| f32::from_bits(words[i])),
                                },
                            });
                        }
                        let info = vk::RenderPassBeginInfo::default()
                            .render_pass(vk::RenderPass::from_raw(pass.raw))
                            .framebuffer(vk::Framebuffer::from_raw(fb.raw))
                            .render_area(vk::Rect2D {
                                offset: vk::Offset2D {
                                    x: a[4] as i32,
                                    y: a[5] as i32,
                                },
                                extent: vk::Extent2D {
                                    width: a[6] as u32,
                                    height: a[7] as u32,
                                },
                            })
                            .clear_values(&clears);
                        unsafe {
                            d.cmd_begin_render_pass(raw, &info, vk::SubpassContents::INLINE);
                        }
                        c.recording.borrow_mut().pass = Some(pass);
                        c.retain(fb);
                    }
                    72 => {
                        if c.recording.borrow().pass.is_none() {
                            return invalid();
                        }
                        unsafe {
                            d.cmd_end_render_pass(raw);
                        }
                        c.recording.borrow_mut().pass = None;
                    }
                    73 => {
                        let r = c.recording.borrow();
                        let pass = r
                            .pass
                            .as_ref()
                            .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
                        let p = r
                            .pipeline
                            .as_ref()
                            .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
                        if p.flags != vk::PipelineBindPoint::GRAPHICS.as_raw() as u32
                            || !r.viewport
                            || !r.scissor
                            || !Rc::ptr_eq(&p.deps.borrow()[1], pass)
                            || a[2..6].iter().any(|&x| x > u32::MAX as u64)
                        {
                            return invalid();
                        }
                        unsafe {
                            d.cmd_draw(raw, a[2] as u32, a[3] as u32, a[4] as u32, a[5] as u32);
                        }
                    }
                    74 => {
                        if c.recording.borrow().pass.is_some() {
                            return invalid();
                        }
                        let image = self.object(a[2], Kind::Image)?;
                        let buffer = self.object(a[4], Kind::Buffer)?;
                        let s = shape(&image)?;
                        if image.deps.borrow().is_empty()
                            || buffer.deps.borrow().is_empty()
                            || image.flags & vk::ImageUsageFlags::TRANSFER_SRC.as_raw() == 0
                            || buffer.flags & vk::BufferUsageFlags::TRANSFER_DST.as_raw() == 0
                            || !matches!(a[3], 1 | 6)
                        {
                            return invalid();
                        }
                        let wire = words64(m, a[5], bounded(a[6], 64)? * 13)?;
                        let mut regions = Vec::new();
                        for x in wire.chunks_exact(13) {
                            checked_copy(x, s, buffer.size)?;
                            regions.push(
                                vk::BufferImageCopy::default()
                                    .buffer_offset(x[0])
                                    .buffer_row_length(x[1] as u32)
                                    .buffer_image_height(x[2] as u32)
                                    .image_subresource(
                                        vk::ImageSubresourceLayers::default()
                                            .aspect_mask(vk::ImageAspectFlags::COLOR)
                                            .layer_count(1),
                                    )
                                    .image_offset(vk::Offset3D {
                                        x: x[7] as i32,
                                        y: x[8] as i32,
                                        z: 0,
                                    })
                                    .image_extent(vk::Extent3D {
                                        width: x[10] as u32,
                                        height: x[11] as u32,
                                        depth: 1,
                                    }),
                            );
                        }
                        unsafe {
                            d.cmd_copy_image_to_buffer(
                                raw,
                                vk::Image::from_raw(image.raw),
                                layout(a[3], false)?,
                                vk::Buffer::from_raw(buffer.raw),
                                &regions,
                            );
                        }
                        c.retain(image);
                        c.retain(buffer);
                    }
                    75 => {
                        let v = words32(m, a[2], 6)?;
                        let v: Vec<_> = v.into_iter().map(f32::from_bits).collect();
                        if v.iter().any(|x| !x.is_finite())
                            || v[0] < 0.0
                            || v[1] < 0.0
                            || v[2] <= 0.0
                            || v[3] <= 0.0
                            || v[0] + v[2] > DIMENSION as f32
                            || v[1] + v[3] > DIMENSION as f32
                            || v[4] < 0.0
                            || v[4] > 1.0
                            || v[5] < 0.0
                            || v[5] > 1.0
                        {
                            return invalid();
                        }
                        let viewport = [vk::Viewport {
                            x: v[0],
                            y: v[1],
                            width: v[2],
                            height: v[3],
                            min_depth: v[4],
                            max_depth: v[5],
                        }];
                        unsafe {
                            d.cmd_set_viewport(raw, 0, &viewport);
                        }
                        c.recording.borrow_mut().viewport = true;
                    }
                    76 => {
                        if a[4] == 0
                            || a[5] == 0
                            || a[2].checked_add(a[4]).is_none_or(|x| x > DIMENSION as u64)
                            || a[3].checked_add(a[5]).is_none_or(|y| y > DIMENSION as u64)
                        {
                            return invalid();
                        }
                        let rect = [vk::Rect2D {
                            offset: vk::Offset2D {
                                x: a[2] as i32,
                                y: a[3] as i32,
                            },
                            extent: vk::Extent2D {
                                width: a[4] as u32,
                                height: a[5] as u32,
                            },
                        }];
                        unsafe {
                            d.cmd_set_scissor(raw, 0, &rect);
                        }
                        c.recording.borrow_mut().scissor = true;
                    }
                    _ => unreachable!(),
                }
            }
            _ => return unsupported(),
        }
        Ok(())
    }
}
fn checked_copy(x: &[u64], image: ImageShape, buffer_size: u64) -> Result<()> {
    if x[3] != 1
        || x[4] != 0
        || x[5] != 0
        || x[6] != 1
        || x[9] != 0
        || x[12] != 1
        || x[10] == 0
        || x[11] == 0
        || x[7]
            .checked_add(x[10])
            .is_none_or(|n| n > image.width as u64)
        || x[8]
            .checked_add(x[11])
            .is_none_or(|n| n > image.height as u64)
        || x[1] != 0 && x[1] < x[10]
        || x[2] != 0 && x[2] < x[11]
        || !x[0].is_multiple_of(4)
        || x[1] > u32::MAX as u64
        || x[2] > u32::MAX as u64
    {
        return invalid();
    }
    let row = if x[1] == 0 { x[10] } else { x[1] };
    if (x[11] - 1)
        .checked_mul(row)
        .and_then(|n| n.checked_add(x[10]))
        .and_then(|n| n.checked_mul(4))
        .and_then(|n| n.checked_add(x[0]))
        .is_none_or(|n| n > buffer_size)
    {
        return invalid();
    }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn copy_checks_padded_rows_offsets_overflow_and_color_subresources() {
        let s = ImageShape {
            width: 64,
            height: 32,
            format: vk::Format::R8G8B8A8_UNORM,
        };
        let valid = [16, 80, 32, 1, 0, 0, 1, 0, 0, 0, 64, 32, 1];
        let bytes = 16 + (31 * 80 + 64) * 4;
        assert!(checked_copy(&valid, s, bytes).is_ok());
        assert!(checked_copy(&valid, s, bytes - 1).is_err());
        for (at, value) in [
            (0, 3),
            (1, 63),
            (2, 31),
            (3, 2),
            (4, 1),
            (5, 1),
            (6, 2),
            (7, 1),
            (8, 1),
            (9, 1),
            (10, 0),
            (11, 0),
            (12, 2),
            (0, u64::MAX),
            (1, u64::MAX),
        ] {
            let mut x = valid;
            x[at] = value;
            assert!(checked_copy(&x, s, u64::MAX).is_err());
        }
    }
    #[test]
    fn image_profile_reports_only_supported_formats_usages_and_layouts() {
        assert!(format(37).is_ok());
        assert!(format(44).is_ok());
        assert!(format(126).is_err());
        assert!(usage(vk::ImageUsageFlags::COLOR_ATTACHMENT.as_raw() as u64).is_ok());
        assert!(usage(vk::ImageUsageFlags::SAMPLED.as_raw() as u64).is_err());
        assert!(usage(0).is_err());
        assert!(usage(1 << 40).is_err());
        assert!(layout(0, true).is_ok());
        assert!(layout(0, false).is_err());
        assert!(layout(1000001002, false).is_err());
    }
}

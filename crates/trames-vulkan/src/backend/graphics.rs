// SPDX-License-Identifier: GPL-3.0-or-later
//! Selected core 1.0 graphics for allocated and borrowed swapchain images. No guest Vulkan structure/pointer is
//! handed to the driver; all arrays are decoded to scoped native records.
use super::*;

#[derive(Clone, Copy)]
pub(super) struct ImageShape {
    pub(super) width: u32,
    pub(super) height: u32,
    pub(super) format: vk::Format,
}
#[derive(Clone, Copy)]
pub(super) struct PassInfo {
    format: vk::Format,
    clear: bool,
    depth: Option<vk::Format>,
    depth_clear: bool,
}
pub(super) struct AttachmentSpec {
    usage: vk::ImageUsageFlags,
    width: u32,
    height: u32,
    formats: Vec<vk::Format>,
}
#[derive(Default)]
pub(super) struct Recording {
    pub pass: Option<Rc<Object>>,
    pub pipeline: Option<Rc<Object>>,
    pass2: bool,
    pub compute_pipeline: Option<Rc<Object>>,
    vertices: BTreeMap<u32, (Rc<Object>, u64)>,
    index: Option<(Rc<Object>, u64, u32)>,
    viewport: bool,
    scissor: bool,
}
#[derive(Clone)]
pub(super) struct VertexBinding {
    binding: u32,
    stride: u32,
    instance: bool,
    extent: u32,
}
// A selected set of common native vertex formats. Image formats remain RGBA8/BGRA8.
fn vertex_size(raw: u64) -> Option<u32> {
    match raw {
        37 | 38 | 41 | 42 | 98..=100 => Some(4),
        101..=103 => Some(8),
        104..=106 => Some(12),
        107..=109 => Some(16),
        _ => None,
    }
}
const DIMENSION: u32 = 4096;
fn format(raw: u64) -> Result<vk::Format> {
    match raw {
        37 => Ok(vk::Format::R8G8B8A8_UNORM),
        43 => Ok(vk::Format::R8G8B8A8_SRGB),
        44 => Ok(vk::Format::B8G8R8A8_UNORM),
        50 => Ok(vk::Format::B8G8R8A8_SRGB),
        124 => Ok(vk::Format::D16_UNORM),
        126 => Ok(vk::Format::D32_SFLOAT),
        _ => Err(vk::Result::ERROR_FORMAT_NOT_SUPPORTED.into()),
    }
}
fn usage(raw: u64) -> Result<vk::ImageUsageFlags> {
    let allowed = vk::ImageUsageFlags::COLOR_ATTACHMENT
        | vk::ImageUsageFlags::TRANSFER_SRC
        | vk::ImageUsageFlags::TRANSFER_DST
        | vk::ImageUsageFlags::SAMPLED
        | vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT;
    if raw == 0 || raw & !(allowed.as_raw() as u64) != 0 {
        return unsupported();
    }
    Ok(vk::ImageUsageFlags::from_raw(raw as u32))
}
fn layout(raw: u64, initial: bool) -> Result<vk::ImageLayout> {
    match raw {
        0 if initial => Ok(vk::ImageLayout::UNDEFINED),
        8 if initial => Ok(vk::ImageLayout::PREINITIALIZED),
        1 => Ok(vk::ImageLayout::GENERAL),
        2 => Ok(vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL),
        3 => Ok(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL),
        4 => Ok(vk::ImageLayout::DEPTH_STENCIL_READ_ONLY_OPTIMAL),
        5 => Ok(vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL),
        6 => Ok(vk::ImageLayout::TRANSFER_SRC_OPTIMAL),
        7 => Ok(vk::ImageLayout::TRANSFER_DST_OPTIMAL),
        1000117000 | 1000117001 => Ok(vk::ImageLayout::from_raw(raw as i32)),
        1000001002 => Ok(vk::ImageLayout::PRESENT_SRC_KHR),
        _ => unsupported(),
    }
}
fn is_depth(format: vk::Format) -> bool {
    matches!(format, vk::Format::D16_UNORM | vk::Format::D32_SFLOAT)
}
fn image_range(format: vk::Format) -> vk::ImageSubresourceRange {
    let mut range = color_range();
    if is_depth(format) {
        range.aspect_mask = vk::ImageAspectFlags::DEPTH;
    }
    range
}
fn format_usage(format: vk::Format, raw: u64) -> Result<vk::ImageUsageFlags> {
    let flags = usage(raw)?;
    if is_depth(format) && flags.contains(vk::ImageUsageFlags::COLOR_ATTACHMENT)
        || !is_depth(format) && flags.contains(vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT)
    {
        return Err(vk::Result::ERROR_FORMAT_NOT_SUPPORTED.into());
    }
    Ok(flags)
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
    fn native_layout(&self, raw: u64, initial: bool) -> Result<vk::ImageLayout> {
        let value = layout(raw, initial)?;
        Ok(
            if self.virtual_wsi && value == vk::ImageLayout::PRESENT_SRC_KHR {
                vk::ImageLayout::GENERAL
            } else {
                value
            },
        )
    }
    // New native resources are owned before the final fallible guest write.
    // A backing I/O failure after preflight must not leak an unpublished object.
    pub(super) fn publish(&mut self, m: &mut Memory, output: u64, id: u64) -> Result<()> {
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
                        | vk::FormatFeatureFlags::DEPTH_STENCIL_ATTACHMENT
                        | vk::FormatFeatureFlags::TRANSFER_SRC
                        | vk::FormatFeatureFlags::TRANSFER_DST
                        | vk::FormatFeatureFlags::SAMPLED_IMAGE
                        | vk::FormatFeatureFlags::SAMPLED_IMAGE_FILTER_LINEAR);
                bytes[4..8].copy_from_slice(&flags.as_raw().to_le_bytes());
            }
            if vertex_size(a[1]).is_some() {
                let p = unsafe {
                    instance.get_physical_device_format_properties(
                        self.physical,
                        vk::Format::from_raw(a[1] as i32),
                    )
                };
                bytes[8..12].copy_from_slice(
                    &(p.buffer_features & vk::FormatFeatureFlags::VERTEX_BUFFER)
                        .as_raw()
                        .to_le_bytes(),
                );
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
        let formats: Vec<_> = words32(m, a[7], bounded(a[8], 16)?)?
            .into_iter()
            .map(|f| format(f as u64))
            .collect::<Result<_>>()?;
        let p = if formats.is_empty() {
            unsafe {
                instance.get_physical_device_image_format_properties(
                    self.physical,
                    format(a[1])?,
                    vk::ImageType::TYPE_2D,
                    vk::ImageTiling::OPTIMAL,
                    format_usage(format(a[1])?, a[4])?,
                    vk::ImageCreateFlags::empty(),
                )
            }?
        } else {
            if self.capabilities.bits & zink::FORMAT_LIST == 0 {
                return unsupported();
            }
            let mut list = vk::ImageFormatListCreateInfo::default().view_formats(&formats);
            let info = vk::PhysicalDeviceImageFormatInfo2::default()
                .format(format(a[1])?)
                .ty(vk::ImageType::TYPE_2D)
                .tiling(vk::ImageTiling::OPTIMAL)
                .usage(format_usage(format(a[1])?, a[4])?)
                .push_next(&mut list);
            let mut properties = vk::ImageFormatProperties2::default();
            let owner = self.instance.as_ref().unwrap();
            let api =
                ash::khr::get_physical_device_properties2::Instance::new(&owner._entry, instance);
            unsafe {
                api.get_physical_device_image_format_properties2(
                    self.physical,
                    &info,
                    &mut properties,
                )
            }?;
            properties.image_format_properties
        };
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
        if a[12] > 1 || a[12] != 0 && state[1] != 4 {
            return unsupported();
        }
        let restart = a[12] != 0;
        if a[14] & !0x73 != 0 {
            return unsupported();
        }
        let depth_test = a[14] & 1 != 0;
        let depth_write = a[14] & 2 != 0;
        let compare = ((a[14] >> 4) & 7) as i32;
        if (depth_test || depth_write) && pass.pass.get().unwrap().depth.is_none() {
            return invalid();
        }
        let cache = if a[13] == 0 {
            None
        } else {
            Some(self.object(a[13], Kind::PipelineCache)?)
        };
        let raw_cache = cache.as_ref().map_or(vk::PipelineCache::null(), |c| {
            vk::PipelineCache::from_raw(c.raw)
        });
        let limits = unsafe {
            device
                .instance
                .native
                .get_physical_device_properties(self.physical)
        }
        .limits;
        // Extended graphics state is copied before the asynchronous native
        // compiler starts; no guest pointers enter that worker.
        let mut config = vec![0; 22];
        config[0] = 3; // Older ICDs supply dynamic viewport/scissor only.
        if a[15] != 0 {
            config = words64(m, a[15], 22)?;
        }
        if config[0] & !3 != 0
            || config[11] > 1
            || [12, 13, 15, 16].iter().any(|&i| config[i] > 14)
            || config[14] > 4
            || config[17] > 4
        {
            return unsupported();
        }
        let static_states = (!config[0] & 3) as u8;
        let mut static_viewports = Vec::new();
        let mut static_scissors = Vec::new();
        if static_states & 1 != 0 {
            let v: Vec<_> = config[1..7]
                .iter()
                .map(|&v| f32::from_bits(v as u32))
                .collect();
            if !valid_viewport(&v, device.maintenance1) {
                return invalid();
            }
            static_viewports.push(vk::Viewport {
                x: v[0],
                y: v[1],
                width: v[2],
                height: v[3],
                min_depth: v[4],
                max_depth: v[5],
            });
        }
        if static_states & 2 != 0 {
            if config[9] == 0
                || config[10] == 0
                || config[7]
                    .checked_add(config[9])
                    .is_none_or(|n| n > DIMENSION as u64)
                || config[8]
                    .checked_add(config[10])
                    .is_none_or(|n| n > DIMENSION as u64)
            {
                return invalid();
            }
            static_scissors.push(vk::Rect2D {
                offset: vk::Offset2D {
                    x: config[7] as i32,
                    y: config[8] as i32,
                },
                extent: vk::Extent2D {
                    width: config[9] as u32,
                    height: config[10] as u32,
                },
            });
        }
        let binding_wire = words64(m, a[8], bounded(a[9], 16)? * 3)?;
        let attribute_wire = words64(m, a[10], bounded(a[11], 16)? * 4)?;
        let mut bindings = Vec::new();
        let mut attributes = Vec::new();
        let mut inputs = BTreeMap::new();
        let mut locations = std::collections::BTreeSet::new();
        for x in binding_wire.chunks_exact(3) {
            if x[0] >= 16.min(limits.max_vertex_input_bindings) as u64
                || x[1] > 2048.min(limits.max_vertex_input_binding_stride) as u64
                || x[2] > 1
                || inputs.contains_key(&(x[0] as u32))
            {
                return unsupported();
            }
            bindings.push(vk::VertexInputBindingDescription {
                binding: x[0] as u32,
                stride: x[1] as u32,
                input_rate: vk::VertexInputRate::from_raw(x[2] as i32),
            });
            inputs.insert(
                x[0] as u32,
                VertexBinding {
                    binding: x[0] as u32,
                    stride: x[1] as u32,
                    instance: x[2] == 1,
                    extent: 0,
                },
            );
        }
        for x in attribute_wire.chunks_exact(4) {
            let size =
                vertex_size(x[2]).ok_or(Failure::Vk(vk::Result::ERROR_FORMAT_NOT_SUPPORTED))?;
            if x[0] >= 16.min(limits.max_vertex_input_attributes) as u64
                || x[1] > u32::MAX as u64
                || x[3] > 2047.min(limits.max_vertex_input_attribute_offset) as u64
                || !locations.insert(x[0])
            {
                return unsupported();
            }
            let input = inputs
                .get_mut(&(x[1] as u32))
                .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
            let format = vk::Format::from_raw(x[2] as i32);
            let features = unsafe {
                device
                    .instance
                    .native
                    .get_physical_device_format_properties(self.physical, format)
            };
            if !features
                .buffer_features
                .contains(vk::FormatFeatureFlags::VERTEX_BUFFER)
            {
                return Err(vk::Result::ERROR_FORMAT_NOT_SUPPORTED.into());
            }
            input.extent = input.extent.max(x[3] as u32 + size);
            attributes.push(vk::VertexInputAttributeDescription {
                location: x[0] as u32,
                binding: x[1] as u32,
                format,
                offset: x[3] as u32,
            });
        }
        let inputs = inputs.into_values().collect();
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
            let vertex = vk::PipelineVertexInputStateCreateInfo::default()
                .vertex_binding_descriptions(&bindings)
                .vertex_attribute_descriptions(&attributes);
            let assembly = vk::PipelineInputAssemblyStateCreateInfo::default()
                .topology(vk::PrimitiveTopology::from_raw(state[1] as i32))
                .primitive_restart_enable(restart);
            let mut viewport = vk::PipelineViewportStateCreateInfo::default()
                .viewport_count(1)
                .scissor_count(1);
            if !static_viewports.is_empty() {
                viewport = viewport.viewports(&static_viewports);
            }
            if !static_scissors.is_empty() {
                viewport = viewport.scissors(&static_scissors);
            }
            let raster = vk::PipelineRasterizationStateCreateInfo::default()
                .polygon_mode(vk::PolygonMode::FILL)
                .cull_mode(vk::CullModeFlags::from_raw(state[2] as u32))
                .front_face(vk::FrontFace::from_raw(state[3] as i32))
                .line_width(1.0);
            let multisample = vk::PipelineMultisampleStateCreateInfo::default()
                .rasterization_samples(vk::SampleCountFlags::TYPE_1);
            let depth = vk::PipelineDepthStencilStateCreateInfo::default()
                .depth_test_enable(depth_test)
                .depth_write_enable(depth_write)
                .depth_compare_op(vk::CompareOp::from_raw(compare));
            let attachments = [vk::PipelineColorBlendAttachmentState::default()
                .blend_enable(config[11] != 0)
                .src_color_blend_factor(vk::BlendFactor::from_raw(config[12] as i32))
                .dst_color_blend_factor(vk::BlendFactor::from_raw(config[13] as i32))
                .color_blend_op(vk::BlendOp::from_raw(config[14] as i32))
                .src_alpha_blend_factor(vk::BlendFactor::from_raw(config[15] as i32))
                .dst_alpha_blend_factor(vk::BlendFactor::from_raw(config[16] as i32))
                .alpha_blend_op(vk::BlendOp::from_raw(config[17] as i32))
                .color_write_mask(vk::ColorComponentFlags::from_raw(state[4] as u32))];
            let blend = vk::PipelineColorBlendStateCreateInfo::default()
                .attachments(&attachments)
                .blend_constants(std::array::from_fn(|i| {
                    f32::from_bits(config[18 + i] as u32)
                }));
            let mut dynamic_states = Vec::new();
            if static_states & 1 == 0 {
                dynamic_states.push(vk::DynamicState::VIEWPORT);
            }
            if static_states & 2 == 0 {
                dynamic_states.push(vk::DynamicState::SCISSOR);
            }
            let dynamic =
                vk::PipelineDynamicStateCreateInfo::default().dynamic_states(&dynamic_states);
            let info = [vk::GraphicsPipelineCreateInfo::default()
                .stages(&stages)
                .vertex_input_state(&vertex)
                .input_assembly_state(&assembly)
                .viewport_state(&viewport)
                .rasterization_state(&raster)
                .multisample_state(&multisample)
                .depth_stencil_state(&depth)
                .color_blend_state(&blend)
                .dynamic_state(&dynamic)
                .layout(raw_layout)
                .render_pass(raw_pass)
                .subpass(0)];
            match unsafe {
                device
                    .native
                    .create_graphics_pipelines(raw_cache, &info, None)
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
                dependencies: {
                    let mut d = vec![pipeline_layout, pass, vertex, fragment];
                    d.extend(cache);
                    d
                },
                bind_point: vk::PipelineBindPoint::GRAPHICS,
                vertex_inputs: inputs,
                static_states,
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
        stride: u64,
    ) -> Result<Vec<vk::ImageMemoryBarrier<'static>>> {
        let stride = match stride {
            0 => 5,
            6 => 6,
            _ => return unsupported(),
        };
        let wire = words64(m, address, bounded(count, 64)? * stride)?;
        let mut barriers = Vec::new();
        let mut dependencies = Vec::new();
        for b in wire.chunks_exact(stride) {
            let image = self.object(b[0], Kind::Image)?;
            if image.deps.borrow().is_empty()
                && image.swapchain.borrow().is_none()
                && image.wayland_pool.borrow().is_none()
            {
                return invalid();
            }
            if image.kind != Kind::SwapchainImage && (b[3] == 1000001002 || b[4] == 1000001002) {
                return invalid();
            }
            let range = image_range(shape(&image)?.format);
            if stride == 6 && b[5] != range.aspect_mask.as_raw() as u64 {
                return invalid();
            }
            barriers.push(
                vk::ImageMemoryBarrier::default()
                    .image(vk::Image::from_raw(image.raw))
                    .src_access_mask(vk::AccessFlags::from_raw(b[1] as u32))
                    .dst_access_mask(vk::AccessFlags::from_raw(b[2] as u32))
                    .old_layout(self.native_layout(b[3], true)?)
                    .new_layout(self.native_layout(b[4], false)?)
                    .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                    .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                    .subresource_range(range),
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
                let u = format_usage(f, a[4])?;
                if a[2] == 0 || a[3] == 0 || a[2] > DIMENSION as u64 || a[3] > DIMENSION as u64 {
                    return invalid();
                }
                let formats: Vec<_> = words32(m, a[7], bounded(a[8], 16)?)?
                    .into_iter()
                    .map(|f| format(f as u64))
                    .collect::<Result<_>>()?;
                if !formats.is_empty() && !device.format_list {
                    return unsupported();
                }
                let mut list = vk::ImageFormatListCreateInfo::default().view_formats(&formats);
                let mut info = vk::ImageCreateInfo::default()
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
                    .initial_layout(match a[6] {
                        0 => vk::ImageLayout::UNDEFINED,
                        8 => vk::ImageLayout::PREINITIALIZED,
                        _ => return invalid(),
                    });
                if !formats.is_empty() {
                    info = info.push_next(&mut list);
                }
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
                if image.kind == Kind::SwapchainImage {
                    return invalid();
                }
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
                if image.kind == Kind::SwapchainImage {
                    return invalid();
                }
                let r = unsafe { d.get_image_memory_requirements(vk::Image::from_raw(image.raw)) };
                if image.swapchain.borrow().is_some()
                    || image.wayland_pool.borrow().is_some()
                    || !image.deps.borrow().is_empty()
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
                if format(a[2])? != shape.format
                    || image.deps.borrow().is_empty()
                        && image.swapchain.borrow().is_none()
                        && image.wayland_pool.borrow().is_none()
                {
                    return invalid();
                }
                let view_usage = if a[4] == 0 {
                    vk::ImageUsageFlags::empty()
                } else {
                    if !device.maintenance2 {
                        return unsupported();
                    }
                    let usage = usage(a[4])?;
                    if usage.as_raw() & !image.flags != 0 {
                        return invalid();
                    }
                    usage
                };
                let mut view_usage_info = vk::ImageViewUsageCreateInfo::default().usage(view_usage);
                let mut info = vk::ImageViewCreateInfo::default()
                    .image(vk::Image::from_raw(image.raw))
                    .view_type(vk::ImageViewType::TYPE_2D)
                    .format(shape.format)
                    .subresource_range(image_range(shape.format));
                if a[4] != 0 {
                    info = info.push_next(&mut view_usage_info);
                }
                let raw = unsafe { d.create_image_view(&info, None) }?;
                let id = self.put(Kind::ImageView, raw.as_raw(), vec![image], 0, 0);
                self.objects[&id].image.set(Some(shape));
                self.publish(m, a[3], id)?;
            }
            65 => self.remove(a[1], Kind::ImageView)?,
            66 | 122 => {
                if op == 122 && !device.renderpass2 {
                    return unsupported();
                }
                self.room(1)?;
                output(m, a[8], 8)?;
                let f = format(a[1])?;
                if a[2] > 2 || a[3] > 1 {
                    return unsupported();
                }
                let mut attachments = vec![vk::AttachmentDescription::default()
                    .format(f)
                    .samples(vk::SampleCountFlags::TYPE_1)
                    .load_op(vk::AttachmentLoadOp::from_raw(a[2] as i32))
                    .store_op(vk::AttachmentStoreOp::from_raw(a[3] as i32))
                    .stencil_load_op(vk::AttachmentLoadOp::DONT_CARE)
                    .stencil_store_op(vk::AttachmentStoreOp::DONT_CARE)
                    .initial_layout(self.native_layout(a[4], true)?)
                    .final_layout(self.native_layout(a[5], false)?)];
                let colors = [vk::AttachmentReference::default()
                    .attachment(0)
                    .layout(vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL)];
                if is_depth(f) {
                    return unsupported();
                }
                let mut depth = None;
                let mut depth_clear = false;
                let mut subpass = vk::SubpassDescription::default()
                    .pipeline_bind_point(vk::PipelineBindPoint::GRAPHICS)
                    .color_attachments(&colors);
                let reference = vk::AttachmentReference::default()
                    .attachment(1)
                    .layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                if a[9] != 0 {
                    let x = words64(m, a[9], 5)?;
                    let df = format(x[0])?;
                    if !is_depth(df) || x[1] > 2 || x[2] > 1 {
                        return unsupported();
                    }
                    attachments.push(
                        vk::AttachmentDescription::default()
                            .format(df)
                            .samples(vk::SampleCountFlags::TYPE_1)
                            .load_op(vk::AttachmentLoadOp::from_raw(x[1] as i32))
                            .store_op(vk::AttachmentStoreOp::from_raw(x[2] as i32))
                            .stencil_load_op(vk::AttachmentLoadOp::DONT_CARE)
                            .stencil_store_op(vk::AttachmentStoreOp::DONT_CARE)
                            .initial_layout(self.native_layout(x[3], true)?)
                            .final_layout(self.native_layout(x[4], false)?),
                    );
                    depth = Some(df);
                    depth_clear = x[1] == 1;
                    subpass = subpass.depth_stencil_attachment(&reference);
                }
                let subpasses = [subpass];
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
                let raw = if op == 122 {
                    let attachments: Vec<_> = attachments
                        .iter()
                        .map(|x| {
                            vk::AttachmentDescription2::default()
                                .format(x.format)
                                .samples(x.samples)
                                .load_op(x.load_op)
                                .store_op(x.store_op)
                                .stencil_load_op(x.stencil_load_op)
                                .stencil_store_op(x.stencil_store_op)
                                .initial_layout(x.initial_layout)
                                .final_layout(x.final_layout)
                        })
                        .collect();
                    let colors = [vk::AttachmentReference2::default()
                        .attachment(0)
                        .layout(vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL)
                        .aspect_mask(vk::ImageAspectFlags::COLOR)];
                    let reference = vk::AttachmentReference2::default()
                        .attachment(1)
                        .layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
                        .aspect_mask(vk::ImageAspectFlags::DEPTH);
                    let mut subpass = vk::SubpassDescription2::default()
                        .pipeline_bind_point(vk::PipelineBindPoint::GRAPHICS)
                        .color_attachments(&colors);
                    if depth.is_some() {
                        subpass = subpass.depth_stencil_attachment(&reference);
                    }
                    let subpasses = [subpass];
                    let dependencies: Vec<_> = dependencies
                        .iter()
                        .map(|x| {
                            vk::SubpassDependency2::default()
                                .src_subpass(x.src_subpass)
                                .dst_subpass(x.dst_subpass)
                                .src_stage_mask(x.src_stage_mask)
                                .dst_stage_mask(x.dst_stage_mask)
                                .src_access_mask(x.src_access_mask)
                                .dst_access_mask(x.dst_access_mask)
                                .dependency_flags(x.dependency_flags)
                        })
                        .collect();
                    let info2 = vk::RenderPassCreateInfo2::default()
                        .attachments(&attachments)
                        .subpasses(&subpasses)
                        .dependencies(&dependencies);
                    let api = ash::khr::create_renderpass2::Device::new(&device.instance.native, d);
                    unsafe { api.create_render_pass2(&info2, None) }?
                } else {
                    unsafe { d.create_render_pass(&info, None) }?
                };
                let id = self.put(Kind::RenderPass, raw.as_raw(), vec![], 0, 0);
                self.objects[&id].pass.set(Some(PassInfo {
                    format: f,
                    clear: a[2] == 1,
                    depth,
                    depth_clear,
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
                let mut attachments = vec![vk::ImageView::from_raw(view.raw)];
                let mut dependencies = vec![pass.clone(), view];
                let depth = pass.pass.get().unwrap().depth;
                if depth.is_some() != (a[6] != 0) {
                    return invalid();
                }
                if let Some(format) = depth {
                    let view = self.object(a[6], Kind::ImageView)?;
                    let ds = shape(&view)?;
                    if ds.format != format
                        || a[3] > ds.width as u64
                        || a[4] > ds.height as u64
                        || view.deps.borrow()[0].flags
                            & vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT.as_raw()
                            == 0
                    {
                        return invalid();
                    }
                    attachments.push(vk::ImageView::from_raw(view.raw));
                    dependencies.push(view);
                }
                let info = vk::FramebufferCreateInfo::default()
                    .render_pass(vk::RenderPass::from_raw(pass.raw))
                    .attachments(&attachments)
                    .width(a[3] as u32)
                    .height(a[4] as u32)
                    .layers(1);
                let raw = unsafe { d.create_framebuffer(&info, None) }?;
                let id = self.put(Kind::Framebuffer, raw.as_raw(), dependencies, 0, 0);
                self.objects[&id].image.set(Some(ImageShape {
                    width: a[3] as u32,
                    height: a[4] as u32,
                    format: s.format,
                }));
                self.publish(m, a[5], id)?;
            }
            125 => {
                if !device.imageless {
                    return unsupported();
                }
                self.room(1)?;
                output(m, a[6], 8)?;
                let pass = self.object(a[1], Kind::RenderPass)?;
                let p = pass.pass.get().unwrap();
                let count = 1 + usize::from(p.depth.is_some());
                if a[4] != count as u64
                    || a[2] == 0
                    || a[3] == 0
                    || a[2] > DIMENSION as u64
                    || a[3] > DIMENSION as u64
                {
                    return invalid();
                }
                let wire = words64(m, a[5], count * 7)?;
                let mut specs = Vec::new();
                for (i, x) in wire.chunks_exact(7).enumerate() {
                    let u = usage(x[1])?;
                    let wanted = if i == 0 { p.format } else { p.depth.unwrap() };
                    let required = if i == 0 {
                        vk::ImageUsageFlags::COLOR_ATTACHMENT
                    } else {
                        vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT
                    };
                    let formats: Vec<_> = words32(m, x[5], bounded(x[6], 16)?)?
                        .into_iter()
                        .map(|f| format(f as u64))
                        .collect::<Result<_>>()?;
                    if x[0] != 0
                        || !u.contains(required)
                        || x[2] < a[2]
                        || x[3] < a[3]
                        || x[2] > DIMENSION as u64
                        || x[3] > DIMENSION as u64
                        || x[4] != 1
                        || !formats.contains(&wanted)
                    {
                        return invalid();
                    }
                    specs.push(AttachmentSpec {
                        usage: u,
                        width: x[2] as u32,
                        height: x[3] as u32,
                        formats,
                    });
                }
                let images: Vec<_> = specs
                    .iter()
                    .map(|s| {
                        vk::FramebufferAttachmentImageInfo::default()
                            .usage(s.usage)
                            .width(s.width)
                            .height(s.height)
                            .layer_count(1)
                            .view_formats(&s.formats)
                    })
                    .collect();
                let mut attachment_info =
                    vk::FramebufferAttachmentsCreateInfo::default().attachment_image_infos(&images);
                let mut info = vk::FramebufferCreateInfo::default()
                    .flags(vk::FramebufferCreateFlags::IMAGELESS)
                    .render_pass(vk::RenderPass::from_raw(pass.raw))
                    .width(a[2] as u32)
                    .height(a[3] as u32)
                    .layers(1)
                    .push_next(&mut attachment_info);
                info.attachment_count = count as u32;
                let raw = unsafe { d.create_framebuffer(&info, None) }?;
                let id = self.put(Kind::Framebuffer, raw.as_raw(), vec![pass], 0, 1);
                self.objects[&id].image.set(Some(ImageShape {
                    width: a[2] as u32,
                    height: a[3] as u32,
                    format: p.format,
                }));
                *self.objects[&id].attachment_specs.borrow_mut() = specs;
                self.publish(m, a[6], id)?;
            }
            69 => self.remove(a[1], Kind::Framebuffer)?,
            71..=76 | 94..=97 | 114 | 123..=124 => {
                let c = self.object(a[1], Kind::CommandBuffer)?;
                if c.state.get() != 1 {
                    return invalid();
                }
                let raw = vk::CommandBuffer::from_raw(c.raw);
                match op {
                    114 => {
                        let image = self.object(a[2], Kind::Image)?;
                        let shape = shape(&image)?;
                        if c.recording.borrow().pass.is_some()
                            || is_depth(shape.format)
                            || image.flags & vk::ImageUsageFlags::TRANSFER_DST.as_raw() == 0
                            || (image.deps.borrow().is_empty()
                                && image.swapchain.borrow().is_none()
                                && image.wayland_pool.borrow().is_none())
                            || a[6] == 0
                        {
                            return invalid();
                        }
                        let native_layout = self.native_layout(a[3], false)?;
                        if !matches!(
                            native_layout,
                            vk::ImageLayout::GENERAL | vk::ImageLayout::TRANSFER_DST_OPTIMAL
                        ) {
                            return invalid();
                        }
                        let color = m.read_array::<16>(a[4])?;
                        let color = vk::ClearColorValue {
                            uint32: std::array::from_fn(|i| {
                                u32::from_le_bytes(color[i * 4..i * 4 + 4].try_into().unwrap())
                            }),
                        };
                        let wire = words64(m, a[5], bounded(a[6], 64)? * 5)?;
                        let mut ranges = Vec::new();
                        for range in wire.chunks_exact(5) {
                            if range[0] != 1
                                || range[1] != 0
                                || !matches!(range[2], 1 | 0xffff_ffff)
                                || range[3] != 0
                                || !matches!(range[4], 1 | 0xffff_ffff)
                            {
                                return invalid();
                            }
                            ranges.push(image_range(shape.format));
                        }
                        unsafe {
                            d.cmd_clear_color_image(
                                raw,
                                vk::Image::from_raw(image.raw),
                                native_layout,
                                &color,
                                &ranges,
                            );
                        }
                        c.retain(image);
                    }
                    71 | 123 => {
                        if op == 123 && !device.renderpass2 {
                            return unsupported();
                        }
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
                            || a[9]
                                > if pass.pass.get().unwrap().depth.is_some() {
                                    2
                                } else {
                                    1
                                }
                            || pass.pass.get().unwrap().clear && a[9] < 1
                            || pass.pass.get().unwrap().depth_clear && a[9] < 2
                        {
                            return invalid();
                        }
                        let mut clears = Vec::new();
                        for index in 0..a[9] {
                            let words = words32(m, a[8] + index * 16, 4)?;
                            if index == 0 {
                                clears.push(vk::ClearValue {
                                    color: vk::ClearColorValue {
                                        float32: std::array::from_fn(|i| f32::from_bits(words[i])),
                                    },
                                });
                            } else {
                                let depth = f32::from_bits(words[0]);
                                if !depth.is_finite()
                                    || !(0.0..=1.0).contains(&depth)
                                    || words[1] != 0
                                {
                                    return invalid();
                                }
                                clears.push(vk::ClearValue {
                                    depth_stencil: vk::ClearDepthStencilValue { depth, stencil: 0 },
                                });
                            }
                        }
                        let views: Vec<_> = words64(m, a[10], bounded(a[11], 2)?)?
                            .into_iter()
                            .map(|id| self.object(id, Kind::ImageView))
                            .collect::<Result<_>>()?;
                        let specs = fb.attachment_specs.borrow();
                        if fb.flags == 1 {
                            if views.len() != specs.len() {
                                return invalid();
                            }
                            let p = pass.pass.get().unwrap();
                            for (i, (v, spec)) in views.iter().zip(specs.iter()).enumerate() {
                                let shape = shape(v)?;
                                let image = &v.deps.borrow()[0];
                                let wanted = if i == 0 { p.format } else { p.depth.unwrap() };
                                if shape.format != wanted
                                    || !spec.formats.contains(&shape.format)
                                    || image.flags != spec.usage.as_raw()
                                    || shape.width < spec.width
                                    || shape.height < spec.height
                                {
                                    return invalid();
                                }
                            }
                        } else if !views.is_empty() {
                            return invalid();
                        }
                        drop(specs);
                        let attachments: Vec<_> = views
                            .iter()
                            .map(|v| vk::ImageView::from_raw(v.raw))
                            .collect();
                        let mut attachment_info =
                            vk::RenderPassAttachmentBeginInfo::default().attachments(&attachments);
                        let mut info = vk::RenderPassBeginInfo::default()
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
                        if fb.flags == 1 {
                            info = info.push_next(&mut attachment_info);
                        }
                        unsafe {
                            if op == 123 {
                                let api = ash::khr::create_renderpass2::Device::new(
                                    &device.instance.native,
                                    d,
                                );
                                api.cmd_begin_render_pass2(
                                    raw,
                                    &info,
                                    &vk::SubpassBeginInfo::default()
                                        .contents(vk::SubpassContents::INLINE),
                                );
                            } else {
                                d.cmd_begin_render_pass(raw, &info, vk::SubpassContents::INLINE);
                            }
                        }
                        for view in views {
                            c.retain(view);
                        }
                        c.recording.borrow_mut().pass2 = op == 123;
                        c.recording.borrow_mut().pass = Some(pass);
                        c.retain(fb);
                    }
                    72 | 124 => {
                        if op == 124 && !device.renderpass2 {
                            return unsupported();
                        }
                        if c.recording.borrow().pass.is_none()
                            || c.recording.borrow().pass2 != (op == 124)
                        {
                            return invalid();
                        }
                        unsafe {
                            if op == 124 {
                                let api = ash::khr::create_renderpass2::Device::new(
                                    &device.instance.native,
                                    d,
                                );
                                api.cmd_end_render_pass2(raw, &vk::SubpassEndInfo::default());
                            } else {
                                d.cmd_end_render_pass(raw);
                            }
                        }
                        c.recording.borrow_mut().pass = None;
                    }
                    73 | 97 => {
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
                            || (!r.viewport && p.static_states.get() & 1 == 0)
                            || (!r.scissor && p.static_states.get() & 2 == 0)
                            || !Rc::ptr_eq(&p.deps.borrow()[1], pass)
                            || a[2..5].iter().any(|&x| x > u32::MAX as u64)
                            || (op == 73 && a[5] > u32::MAX as u64)
                            || (op == 97
                                && ((a[5] as i64) != (a[5] as i32 as i64)
                                    || a[6] > u32::MAX as u64))
                        {
                            return invalid();
                        }
                        let active = a[2] != 0 && a[3] != 0;
                        for input in p.vertex_inputs.borrow().iter().filter(|x| x.extent != 0) {
                            let (buffer, offset) = r
                                .vertices
                                .get(&input.binding)
                                .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
                            // GPU-resident index values stay on the GPU. Check the index
                            // range below; as on Vulkan, indexed vertex values are the
                            // application's responsibility, not a CPU readback/scan.
                            let last = if !active || op == 97 && !input.instance {
                                0
                            } else if input.instance {
                                (if op == 97 { a[6] } else { a[5] })
                                    .checked_add(a[3] - 1)
                                    .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?
                            } else {
                                a[4].checked_add(a[2] - 1)
                                    .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?
                            };
                            if active
                                && last
                                    .checked_mul(input.stride as u64)
                                    .and_then(|n| n.checked_add(*offset))
                                    .and_then(|n| n.checked_add(input.extent as u64))
                                    .is_none_or(|n| n > buffer.size)
                            {
                                return invalid();
                            }
                        }
                        if op == 97 {
                            let (buffer, offset, size) = r
                                .index
                                .as_ref()
                                .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
                            if a[4]
                                .checked_add(a[2])
                                .and_then(|n| n.checked_mul(*size as u64))
                                .and_then(|n| n.checked_add(*offset))
                                .is_none_or(|n| n > buffer.size)
                            {
                                return invalid();
                            }
                            unsafe {
                                d.cmd_draw_indexed(
                                    raw,
                                    a[2] as u32,
                                    a[3] as u32,
                                    a[4] as u32,
                                    a[5] as i32,
                                    a[6] as u32,
                                );
                            }
                        } else {
                            unsafe {
                                d.cmd_draw(raw, a[2] as u32, a[3] as u32, a[4] as u32, a[5] as u32);
                            }
                        }
                    }
                    74 | 94 => {
                        if c.recording.borrow().pass.is_some() {
                            return invalid();
                        }
                        let image = self.object(a[2], Kind::Image)?;
                        let buffer = self.object(a[4], Kind::Buffer)?;
                        let s = shape(&image)?;
                        if image.deps.borrow().is_empty()
                            && image.swapchain.borrow().is_none()
                            && image.wayland_pool.borrow().is_none()
                            || buffer.deps.borrow().is_empty()
                            || image.flags
                                & (if op == 94 {
                                    vk::ImageUsageFlags::TRANSFER_DST
                                } else {
                                    vk::ImageUsageFlags::TRANSFER_SRC
                                })
                                .as_raw()
                                == 0
                            || buffer.flags
                                & (if op == 94 {
                                    vk::BufferUsageFlags::TRANSFER_SRC
                                } else {
                                    vk::BufferUsageFlags::TRANSFER_DST
                                })
                                .as_raw()
                                == 0
                            || a[3] != 1 && a[3] != if op == 94 { 7 } else { 6 }
                            || a[6] == 0
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
                            if op == 94 {
                                d.cmd_copy_buffer_to_image(
                                    raw,
                                    vk::Buffer::from_raw(buffer.raw),
                                    vk::Image::from_raw(image.raw),
                                    self.native_layout(a[3], false)?,
                                    &regions,
                                );
                            } else {
                                d.cmd_copy_image_to_buffer(
                                    raw,
                                    vk::Image::from_raw(image.raw),
                                    self.native_layout(a[3], false)?,
                                    vk::Buffer::from_raw(buffer.raw),
                                    &regions,
                                );
                            }
                        }
                        c.retain(image);
                        c.retain(buffer);
                    }
                    95 => {
                        if a[3] == 0 || a[2].checked_add(a[3]).is_none_or(|n| n > 16) {
                            return unsupported();
                        }
                        let ids = words64(m, a[4], bounded(a[3], 16)?)?;
                        let offsets = words64(m, a[5], ids.len())?;
                        let mut buffers = Vec::new();
                        let mut objects = Vec::new();
                        for (&id, &offset) in ids.iter().zip(&offsets) {
                            let b = self.object(id, Kind::Buffer)?;
                            if b.flags & vk::BufferUsageFlags::VERTEX_BUFFER.as_raw() == 0
                                || b.deps.borrow().is_empty()
                                || offset >= b.size
                            {
                                return invalid();
                            }
                            buffers.push(vk::Buffer::from_raw(b.raw));
                            objects.push(b);
                        }
                        unsafe {
                            d.cmd_bind_vertex_buffers(raw, a[2] as u32, &buffers, &offsets);
                        }
                        for (i, (b, offset)) in objects.into_iter().zip(offsets).enumerate() {
                            c.recording
                                .borrow_mut()
                                .vertices
                                .insert(a[2] as u32 + i as u32, (b.clone(), offset));
                            c.retain(b);
                        }
                    }
                    96 => {
                        let b = self.object(a[2], Kind::Buffer)?;
                        let size = match a[4] {
                            0 => 2,
                            1 => 4,
                            _ => return unsupported(),
                        };
                        if b.flags & vk::BufferUsageFlags::INDEX_BUFFER.as_raw() == 0
                            || b.deps.borrow().is_empty()
                            || a[3] >= b.size
                            || !a[3].is_multiple_of(size as u64)
                        {
                            return invalid();
                        }
                        unsafe {
                            d.cmd_bind_index_buffer(
                                raw,
                                vk::Buffer::from_raw(b.raw),
                                a[3],
                                vk::IndexType::from_raw(a[4] as i32),
                            );
                        }
                        c.recording.borrow_mut().index = Some((b.clone(), a[3], size));
                        c.retain(b);
                    }
                    75 => {
                        let v = words32(m, a[2], 6)?;
                        let v: Vec<_> = v.into_iter().map(f32::from_bits).collect();
                        if !valid_viewport(&v, device.maintenance1) {
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
fn valid_viewport(v: &[f32], maintenance1: bool) -> bool {
    v.iter().all(|v| v.is_finite())
        && v[0] >= 0.0
        && v[1] >= 0.0
        && v[2] > 0.0
        && (v[3] > 0.0 || maintenance1)
        && v[0] + v[2] <= DIMENSION as f32
        && v[1] <= DIMENSION as f32
        && (0.0..=DIMENSION as f32).contains(&(v[1] + v[3]))
        && (0.0..=1.0).contains(&v[4])
        && (0.0..=1.0).contains(&v[5])
}

fn checked_copy(x: &[u64], image: ImageShape, buffer_size: u64) -> Result<()> {
    if is_depth(image.format) {
        return unsupported();
    }
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
    fn maintenance1_negative_viewports_use_native_bounds() {
        let flipped = [0.0, 192.0, 256.0, -192.0, 0.0, 1.0];
        assert!(valid_viewport(&flipped, true));
        assert!(valid_viewport(&[0.0, 0.0, 256.0, 0.0, 0.0, 1.0], true));
        assert!(!valid_viewport(&flipped, false));
        assert!(valid_viewport(&[0.0, 0.0, 256.0, 192.0, 0.0, 1.0], false));
        for (field, value) in [
            (0, f32::NAN),
            (1, 0.0),
            (2, 0.0),
            (3, -193.0),
            (4, -0.1),
            (5, 1.1),
        ] {
            let mut invalid = flipped;
            invalid[field] = value;
            assert!(!valid_viewport(&invalid, true));
        }
    }
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
        assert_eq!(format(126).unwrap(), vk::Format::D32_SFLOAT);
        assert!(is_depth(vk::Format::D32_SFLOAT));
        assert!(is_depth(vk::Format::D16_UNORM));
        assert!(!is_depth(vk::Format::R8G8B8A8_UNORM));
        assert!(format(127).is_err());
        assert!(usage(vk::ImageUsageFlags::COLOR_ATTACHMENT.as_raw() as u64).is_ok());
        assert!(usage(vk::ImageUsageFlags::SAMPLED.as_raw() as u64).is_ok());
        assert!(usage(0).is_err());
        assert!(usage(1 << 40).is_err());
        assert!(layout(0, true).is_ok());
        assert!(layout(0, false).is_err());
        assert!(layout(1000001002, false).is_ok());
    }
}

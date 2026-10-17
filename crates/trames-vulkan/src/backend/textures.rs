// SPDX-License-Identifier: GPL-3.0-or-later
//! Sampled image descriptors and original-driver samplers. Wire records own no
//! host pointers; descriptor slots retain both halves of a combined sampler.
use super::*;

impl Backend {
    pub(super) fn sampler_call(&mut self, m: &mut Memory, op: u64, a: [u64; 16]) -> Result<()> {
        if op == 93 {
            return self.remove(a[1], Kind::Sampler);
        }
        self.room(1)?;
        output(m, a[9], 8)?;
        let lod = words32(m, a[8], 3)?;
        let lod: Vec<_> = lod.into_iter().map(f32::from_bits).collect();
        let device = self.device(a[0])?;
        let limits = unsafe {
            device
                .instance
                .native
                .get_physical_device_properties(self.physical)
        }
        .limits;
        if a[1..=3].iter().any(|&x| x > 1)
            || a[4..=6].iter().any(|&x| x > 3)
            || a[7] > 5
            || lod.iter().any(|x| !x.is_finite())
            || lod[0].abs() > limits.max_sampler_lod_bias
            || lod[1] < 0.0
            || lod[2] < lod[1]
        {
            return unsupported();
        }
        let anisotropy = f32::from_bits(a[11] as u32);
        if a[10] > 1
            || a[10] != 0
                && (!device.anisotropy
                    || !anisotropy.is_finite()
                    || anisotropy < 1.0
                    || anisotropy > limits.max_sampler_anisotropy)
        {
            return unsupported();
        }
        let info = vk::SamplerCreateInfo::default()
            .anisotropy_enable(a[10] != 0)
            .max_anisotropy(if a[10] != 0 { anisotropy } else { 1.0 })
            .mag_filter(vk::Filter::from_raw(a[1] as i32))
            .min_filter(vk::Filter::from_raw(a[2] as i32))
            .mipmap_mode(vk::SamplerMipmapMode::from_raw(a[3] as i32))
            .address_mode_u(vk::SamplerAddressMode::from_raw(a[4] as i32))
            .address_mode_v(vk::SamplerAddressMode::from_raw(a[5] as i32))
            .address_mode_w(vk::SamplerAddressMode::from_raw(a[6] as i32))
            .border_color(vk::BorderColor::from_raw(a[7] as i32))
            .mip_lod_bias(lod[0])
            .min_lod(lod[1])
            .max_lod(lod[2]);
        let raw = unsafe { device.native.create_sampler(&info, None) }?;
        let id = self.put(Kind::Sampler, raw.as_raw(), vec![], 0, 0);
        self.publish(m, a[9], id)
    }

    pub(super) fn write_descriptors(&self, m: &Memory, a: [u64; 16]) -> Result<()> {
        let set = self.object(a[1], Kind::DescriptorSet)?;
        if !matches!(a[5], 0..=2 | 6 | 7) {
            return unsupported();
        }
        let layout = set.deps.borrow()[1].clone();
        let metadata = layout.bindings.borrow();
        let shape = metadata
            .get(&(a[2] as u32))
            .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
        if a[2] > u32::MAX as u64
            || a[4] == 0
            || shape.0 as u64 != a[5]
            || a[3].checked_add(a[4]).is_none_or(|n| n > shape.1 as u64)
        {
            return invalid();
        }
        let wire = words64(m, a[6], bounded(a[4], 64)? * 3)?;
        let d = &set.device.native;
        let limits = unsafe {
            set.device
                .instance
                .native
                .get_physical_device_properties(self.physical)
        }
        .limits;
        let mut buffers = Vec::new();
        let mut images = Vec::new();
        let mut dependencies = Vec::new();
        for x in wire.chunks_exact(3) {
            let mut deps = Vec::new();
            if a[5] >= 6 {
                let buffer = self.object(x[0], Kind::Buffer)?;
                let (usage, alignment, max_range) = if a[5] == 6 {
                    (
                        vk::BufferUsageFlags::UNIFORM_BUFFER,
                        limits.min_uniform_buffer_offset_alignment,
                        limits.max_uniform_buffer_range,
                    )
                } else {
                    (
                        vk::BufferUsageFlags::STORAGE_BUFFER,
                        limits.min_storage_buffer_offset_alignment,
                        limits.max_storage_buffer_range,
                    )
                };
                let range = if x[2] == vk::WHOLE_SIZE {
                    buffer.size.saturating_sub(x[1])
                } else {
                    x[2]
                };
                if buffer.deps.borrow().is_empty()
                    || buffer.flags & usage.as_raw() == 0
                    || !x[1].is_multiple_of(alignment.max(1))
                    || range == 0
                    || range > max_range as u64
                    || x[1].checked_add(range).is_none_or(|n| n > buffer.size)
                {
                    return invalid();
                }
                buffers.push(
                    vk::DescriptorBufferInfo::default()
                        .buffer(vk::Buffer::from_raw(buffer.raw))
                        .offset(x[1])
                        .range(x[2]),
                );
                deps.push(buffer);
            } else {
                let mut info = vk::DescriptorImageInfo::default();
                if a[5] != 2 {
                    let sampler = self.object(x[0], Kind::Sampler)?;
                    info.sampler = vk::Sampler::from_raw(sampler.raw);
                    deps.push(sampler);
                }
                if a[5] != 0 {
                    let view = self.object(x[1], Kind::ImageView)?;
                    let image = view.deps.borrow()[0].clone();
                    if image.flags & vk::ImageUsageFlags::SAMPLED.as_raw() == 0
                        || !matches!(x[2], 1 | 5)
                    {
                        return invalid();
                    }
                    info.image_view = vk::ImageView::from_raw(view.raw);
                    info.image_layout = vk::ImageLayout::from_raw(x[2] as i32);
                    deps.push(view);
                }
                images.push(info);
            }
            dependencies.push(deps);
        }
        let write = vk::WriteDescriptorSet::default()
            .dst_set(vk::DescriptorSet::from_raw(set.raw))
            .dst_binding(a[2] as u32)
            .dst_array_element(a[3] as u32)
            .descriptor_type(vk::DescriptorType::from_raw(a[5] as i32));
        let write = if a[5] >= 6 {
            write.buffer_info(&buffers)
        } else {
            write.image_info(&images)
        };
        unsafe {
            d.update_descriptor_sets(&[write], &[]);
        }
        let mut slots = set.bound_resources.borrow_mut();
        for (i, deps) in dependencies.into_iter().enumerate() {
            slots.insert((a[2] as u32, a[3] as u32 + i as u32), deps);
        }
        set.deps.borrow_mut().truncate(2);
        for resource in slots.values().flatten() {
            set.retain(resource.clone());
        }
        Ok(())
    }
}

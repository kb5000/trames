// SPDX-License-Identifier: GPL-3.0-or-later
//! Promoted features use original driver extension entrypoints on the 1.0 host
//! instance. Only copied scalar/array payloads cross the guest boundary.
use super::*;

pub(super) const MAINTENANCE1: u64 = 1;
pub(super) const TIMELINE: u64 = 2;
pub(super) const DRIVER_PROPERTIES: u64 = 4;

#[derive(Default)]
pub(super) struct Capabilities {
    pub bits: u64,
    pub max_timeline_difference: u64,
    driver: Option<vk::PhysicalDeviceDriverProperties<'static>>,
}
impl Capabilities {
    pub fn query(instance: &Instance, physical: vk::PhysicalDevice) -> Result<Self> {
        let ext = unsafe {
            instance
                .native
                .enumerate_device_extension_properties(physical)
        }?;
        let has = |name: &std::ffi::CStr| {
            ext.iter()
                .any(|e| unsafe { std::ffi::CStr::from_ptr(e.extension_name.as_ptr()) == name })
        };
        let mut result = Self::default();
        if has(ash::khr::maintenance1::NAME) {
            result.bits |= MAINTENANCE1;
        }
        if !instance.queries2 {
            return Ok(result);
        }
        let queries = ash::khr::get_physical_device_properties2::Instance::new(
            &instance._entry,
            &instance.native,
        );
        if has(ash::khr::timeline_semaphore::NAME) {
            let mut timeline = vk::PhysicalDeviceTimelineSemaphoreFeatures::default();
            let mut features = vk::PhysicalDeviceFeatures2::default().push_next(&mut timeline);
            unsafe {
                queries.get_physical_device_features2(physical, &mut features);
            }
            if timeline.timeline_semaphore != 0 {
                let mut timeline = vk::PhysicalDeviceTimelineSemaphoreProperties::default();
                let mut properties =
                    vk::PhysicalDeviceProperties2::default().push_next(&mut timeline);
                unsafe {
                    queries.get_physical_device_properties2(physical, &mut properties);
                }
                result.bits |= TIMELINE;
                result.max_timeline_difference = timeline.max_timeline_semaphore_value_difference;
            }
        }
        if has(ash::khr::driver_properties::NAME) {
            let mut driver = vk::PhysicalDeviceDriverProperties::default();
            let mut properties = vk::PhysicalDeviceProperties2::default().push_next(&mut driver);
            unsafe {
                queries.get_physical_device_properties2(physical, &mut properties);
            }
            // The retained struct contains no host pNext link or borrowed data.
            driver.p_next = std::ptr::null_mut();
            result.driver = Some(driver);
            result.bits |= DRIVER_PROPERTIES;
        }
        Ok(result)
    }
    pub fn write_driver(&self, m: &mut Memory, at: u64) -> Result<()> {
        let Some(driver) = &self.driver else {
            return unsupported();
        };
        let mut bytes = Vec::with_capacity(520);
        bytes.extend_from_slice(&driver.driver_id.as_raw().to_le_bytes());
        bytes.extend(driver.driver_name.iter().map(|&x| x as u8));
        bytes.extend(driver.driver_info.iter().map(|&x| x as u8));
        let c = driver.conformance_version;
        bytes.extend_from_slice(&[c.major, c.minor, c.subminor, c.patch]);
        write(m, at, &bytes)
    }
}

pub(super) fn device_extensions(
    flags: u64,
    caps: &Capabilities,
    names: &mut Vec<*const std::ffi::c_char>,
) -> Result<()> {
    // bit8 enables the timeline extension, bit2 separately enables its feature.
    if flags & !15 != 0
        || flags & 7 & !caps.bits != 0
        || flags & 2 != 0 && flags & 8 == 0
        || flags & 8 != 0 && caps.bits & TIMELINE == 0
    {
        return unsupported();
    }
    if flags & MAINTENANCE1 != 0 {
        names.push(ash::khr::maintenance1::NAME.as_ptr());
    }
    if flags & 8 != 0 {
        names.push(ash::khr::timeline_semaphore::NAME.as_ptr());
    }
    if flags & DRIVER_PROPERTIES != 0 {
        names.push(ash::khr::driver_properties::NAME.as_ptr());
    }
    Ok(())
}

impl Backend {
    pub(super) fn zink_device(
        &mut self,
        m: &mut Memory,
        op: u64,
        a: [u64; 16],
        dev: &Arc<Device>,
    ) -> Result<()> {
        let d = &dev.native;
        if op == 116 {
            if !dev.maintenance1 {
                return unsupported();
            }
            if a[2] != 0 {
                return invalid();
            }
            let pool = self.object(a[1], Kind::CommandPool)?;
            let api = ash::khr::maintenance1::Device::new(&dev.instance.native, d);
            unsafe {
                api.trim_command_pool(
                    vk::CommandPool::from_raw(pool.raw),
                    vk::CommandPoolTrimFlags::empty(),
                );
            }
            return Ok(());
        }
        if !dev.timeline {
            return unsupported();
        }
        let api = ash::khr::timeline_semaphore::Device::new(&dev.instance.native, d);
        if op == 119 {
            let count = bounded(a[3], 64)?;
            if count == 0 || a[4] & !1 != 0 {
                return invalid();
            }
            let values = words64(m, a[2], count)?;
            let objects: Vec<_> = words64(m, a[1], count)?
                .into_iter()
                .map(|id| self.object(id, Kind::Semaphore))
                .collect::<Result<_>>()?;
            if objects.iter().any(|s| s.flags != 1) {
                return invalid();
            }
            let semaphores: Vec<_> = objects
                .iter()
                .map(|s| vk::Semaphore::from_raw(s.raw))
                .collect();
            let info = vk::SemaphoreWaitInfo::default()
                .flags(vk::SemaphoreWaitFlags::from_raw(a[4] as u32))
                .semaphores(&semaphores)
                .values(&values);
            // A single native poll only. Guest waits yield through nanosleep;
            // no host worker/guest-memory guard is held across a blocking wait.
            unsafe { api.wait_semaphores(&info, 0) }?;
            self.coherent_sync(m, false)?;
        } else {
            if op == 117 {
                output(m, a[2], 8)?;
            }
            let semaphore = self.object(a[1], Kind::Semaphore)?;
            if semaphore.flags != 1 {
                return invalid();
            }
            let native = vk::Semaphore::from_raw(semaphore.raw);
            match op {
                117 => {
                    let value = unsafe { api.get_semaphore_counter_value(native) }?;
                    write(m, a[2], &value.to_le_bytes())?;
                }
                118 => {
                    self.coherent_sync(m, true)?;
                    let info = vk::SemaphoreSignalInfo::default()
                        .semaphore(native)
                        .value(a[2]);
                    unsafe { api.signal_semaphore(&info) }?;
                }
                _ => return unsupported(),
            }
        }
        Ok(())
    }
}

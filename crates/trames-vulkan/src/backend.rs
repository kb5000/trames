// SPDX-License-Identifier: GPL-3.0-or-later
//! Driver objects stay on the host. IDs and copied bytes are the only wire data.
use ash::{vk, Entry};
use std::{
    cell::{Cell, RefCell},
    collections::BTreeMap,
    rc::{Rc, Weak},
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
};
use trames_memory::{Access, Memory, MemoryError};
use vk::Handle;

const LIMIT: usize = 256;
const MAX_ALLOCATION: u64 = 64 << 20;
static DOMAIN: AtomicU64 = AtomicU64::new(1);
type Result<T> = std::result::Result<T, Failure>;
#[derive(Debug)]
enum Failure {
    Memory(MemoryError),
    Vk(vk::Result),
}
impl From<MemoryError> for Failure {
    fn from(e: MemoryError) -> Self {
        Self::Memory(e)
    }
}
impl From<vk::Result> for Failure {
    fn from(e: vk::Result) -> Self {
        Self::Vk(e)
    }
}
fn invalid<T>() -> Result<T> {
    Err(Failure::Vk(vk::Result::ERROR_UNKNOWN))
}
fn unsupported<T>() -> Result<T> {
    Err(Failure::Vk(vk::Result::ERROR_FEATURE_NOT_PRESENT))
}

struct Instance {
    native: ash::Instance,
    _entry: Entry,
}
impl Drop for Instance {
    fn drop(&mut self) {
        unsafe { self.native.destroy_instance(None) }
    }
}
struct Device {
    native: ash::Device,
    instance: Arc<Instance>,
    atom: u64,
    map_alignment: u64,
}
impl Drop for Device {
    fn drop(&mut self) {
        // Final teardown only: no guest memory/worker guard is held here.
        unsafe {
            let _ = self.native.device_wait_idle();
            self.native.destroy_device(None);
        }
    }
}
#[derive(Clone, Copy, PartialEq, Eq)]
enum Kind {
    Buffer,
    Memory,
    Shader,
    DescriptorLayout,
    PipelineLayout,
    Pipeline,
    DescriptorPool,
    DescriptorSet,
    CommandPool,
    CommandBuffer,
    Fence,
    Semaphore,
}
struct Object {
    kind: Kind,
    raw: u64,
    device: Arc<Device>,
    deps: RefCell<Vec<Rc<Object>>>,
    bindings: RefCell<BTreeMap<u32, (u32, u32)>>,
    bound_buffers: RefCell<BTreeMap<(u32, u32), Rc<Object>>>,
    push_ranges: RefCell<Vec<(u32, u32)>>,
    size: u64,
    flags: u32,
    state: Cell<u8>,
    mapped: Cell<usize>,
    budget: crate::Budget,
}
impl Object {
    fn retain(&self, other: Rc<Object>) {
        let mut deps = self.deps.borrow_mut();
        if !deps.iter().any(|x| Rc::ptr_eq(x, &other)) {
            deps.push(other);
        }
    }
}
impl Drop for Object {
    fn drop(&mut self) {
        let d = &self.device.native;
        // References from bindings, recorded commands and in-flight submissions
        // keep every dependency alive until the native driver has finished.
        unsafe {
            match self.kind {
                Kind::Buffer => d.destroy_buffer(vk::Buffer::from_raw(self.raw), None),
                Kind::Memory => {
                    if self.mapped.get() != 0 {
                        d.unmap_memory(vk::DeviceMemory::from_raw(self.raw));
                    }
                    d.free_memory(vk::DeviceMemory::from_raw(self.raw), None);
                    self.budget.release(self.size);
                }
                Kind::Shader => d.destroy_shader_module(vk::ShaderModule::from_raw(self.raw), None),
                Kind::DescriptorLayout => d.destroy_descriptor_set_layout(
                    vk::DescriptorSetLayout::from_raw(self.raw),
                    None,
                ),
                Kind::PipelineLayout => {
                    d.destroy_pipeline_layout(vk::PipelineLayout::from_raw(self.raw), None)
                }
                Kind::Pipeline => d.destroy_pipeline(vk::Pipeline::from_raw(self.raw), None),
                Kind::DescriptorPool => {
                    d.destroy_descriptor_pool(vk::DescriptorPool::from_raw(self.raw), None)
                }
                Kind::DescriptorSet => {
                    let pool = self.deps.borrow()[0].raw;
                    let _ = d.free_descriptor_sets(
                        vk::DescriptorPool::from_raw(pool),
                        &[vk::DescriptorSet::from_raw(self.raw)],
                    );
                }
                Kind::CommandPool => {
                    d.destroy_command_pool(vk::CommandPool::from_raw(self.raw), None)
                }
                Kind::CommandBuffer => {
                    let pool = self.deps.borrow()[0].raw;
                    d.free_command_buffers(
                        vk::CommandPool::from_raw(pool),
                        &[vk::CommandBuffer::from_raw(self.raw)],
                    );
                }
                Kind::Fence => d.destroy_fence(vk::Fence::from_raw(self.raw), None),
                Kind::Semaphore => d.destroy_semaphore(vk::Semaphore::from_raw(self.raw), None),
            }
        }
    }
}
struct Submission {
    fence: Rc<Object>,
    commands: Vec<Rc<Object>>,
    _resources: Vec<Rc<Object>>,
}
struct Mapping {
    memory: Rc<Object>,
    guest: u64,
    offset: u64,
    size: u64,
}
#[derive(Default)]
struct SubmitBatch {
    waits: Vec<vk::Semaphore>,
    stages: Vec<vk::PipelineStageFlags>,
    commands: Vec<vk::CommandBuffer>,
    signals: Vec<vk::Semaphore>,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Invocation {
    Complete(u64),
    Pending(u64),
}
#[derive(Clone, Copy, Debug, Default)]
pub struct TransferMetrics {
    pub upload_bytes: u64,
    pub download_bytes: u64,
    pub direct_bytes: u64,
    pub buffered_bytes: u64,
}
struct CompiledPipeline {
    device: Arc<Device>,
    raw: vk::Pipeline,
}
impl Drop for CompiledPipeline {
    fn drop(&mut self) {
        if self.raw != vk::Pipeline::null() {
            unsafe {
                self.device.native.destroy_pipeline(self.raw, None);
            }
        }
    }
}
struct Compile {
    // Task drops first: it must finish before these Rc-owned dependencies drop.
    task: crate::task::Task<std::result::Result<CompiledPipeline, vk::Result>>,
    layout: Rc<Object>,
    _shader: Rc<Object>,
    output: u64,
    abandoned: bool,
}

pub struct Backend {
    budget: crate::Budget,
    domain: u64,
    next: u64,
    instance: Option<Arc<Instance>>,
    physical: vk::PhysicalDevice,
    instance_id: u64,
    physical_id: u64,
    device_id: u64,
    queue_id: u64,
    device: Option<Arc<Device>>,
    queue: vk::Queue,
    family: u32,
    objects: BTreeMap<u64, Rc<Object>>,
    retained: Vec<Weak<Object>>,
    mappings: BTreeMap<u64, Mapping>,
    submissions: Vec<Submission>,
    compiles: BTreeMap<u64, Compile>,
    pub transfers: TransferMetrics,
}
impl Default for Backend {
    fn default() -> Self {
        Self::new()
    }
}
impl Backend {
    pub fn new() -> Self {
        Self::with_budget(crate::Budget::default())
    }
    pub fn with_budget(budget: crate::Budget) -> Self {
        Self {
            budget,
            domain: DOMAIN.fetch_add(1, Ordering::Relaxed),
            next: 1,
            instance: None,
            physical: vk::PhysicalDevice::null(),
            instance_id: 0,
            physical_id: 0,
            device_id: 0,
            queue_id: 0,
            device: None,
            queue: vk::Queue::null(),
            family: 0,
            objects: BTreeMap::new(),
            retained: Vec::new(),
            mappings: BTreeMap::new(),
            submissions: Vec::new(),
            compiles: BTreeMap::new(),
            transfers: TransferMetrics::default(),
        }
    }
    fn id(&mut self) -> u64 {
        let id = (self.domain << 32) | self.next;
        self.next += 1;
        id
    }
    fn room(&self, n: usize) -> Result<()> {
        if self
            .retained
            .iter()
            .filter(|x| x.strong_count() != 0)
            .count()
            + self.compiles.len()
            + n
            > LIMIT
        {
            Err(vk::Result::ERROR_OUT_OF_HOST_MEMORY.into())
        } else {
            Ok(())
        }
    }
    fn device(&self, id: u64) -> Result<Arc<Device>> {
        if id != self.device_id || id == 0 {
            return invalid();
        }
        self.device
            .clone()
            .ok_or(Failure::Vk(vk::Result::ERROR_DEVICE_LOST))
    }
    fn object(&self, id: u64, kind: Kind) -> Result<Rc<Object>> {
        self.objects
            .get(&id)
            .filter(|x| x.kind == kind)
            .cloned()
            .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))
    }
    fn put(&mut self, kind: Kind, raw: u64, deps: Vec<Rc<Object>>, size: u64, flags: u32) -> u64 {
        let object = Rc::new(Object {
            kind,
            raw,
            device: self.device.as_ref().unwrap().clone(),
            deps: RefCell::new(deps),
            bindings: RefCell::new(BTreeMap::new()),
            bound_buffers: RefCell::new(BTreeMap::new()),
            push_ranges: RefCell::new(Vec::new()),
            size,
            flags,
            state: Cell::new(0),
            mapped: Cell::new(0),
            budget: self.budget.clone(),
        });
        let id = self.id();
        self.retained.push(Rc::downgrade(&object));
        self.objects.insert(id, object);
        id
    }
    fn remove(&mut self, id: u64, kind: Kind) -> Result<()> {
        let parent = self.object(id, kind)?;
        if matches!(kind, Kind::CommandPool | Kind::DescriptorPool) {
            self.objects.retain(|_, x| {
                !x.deps
                    .borrow()
                    .first()
                    .is_some_and(|p| Rc::ptr_eq(p, &parent))
            });
        }
        self.mappings.remove(&id);
        self.objects.remove(&id);
        Ok(())
    }
    fn submit(&mut self, m: &Memory, op: u64, a: [u64; 16], dev: Arc<Device>) -> Result<()> {
        if a[1] != self.queue_id {
            return invalid();
        }
        if self.submissions.len() >= 64 {
            return Err(vk::Result::ERROR_OUT_OF_HOST_MEMORY.into());
        }
        // Keep operation 52 readable for the initial ICD. Operation 56 retains
        // each VkSubmitInfo boundary instead of merging semaphore dependencies.
        let wire = if op == 52 {
            vec![0, 0, 0, a[2], a[3], 0, 0]
        } else {
            words64(m, a[2], bounded(a[3], 64)? * 7)?
        };
        let mut batches = Vec::new();
        let mut commands = Vec::new();
        let mut semaphores = Vec::new();
        let mut semaphore_states = BTreeMap::new();
        let (mut total_waits, mut total_commands, mut total_signals) = (0, 0, 0);
        for record in wire.chunks_exact(7) {
            let waits = bounded(record[1], 64)?;
            let count = bounded(record[4], 64)?;
            let signals = bounded(record[6], 64)?;
            total_waits += waits;
            total_commands += count;
            total_signals += signals;
            if total_waits > 64 || total_commands > 64 || total_signals > 64 {
                return unsupported();
            }
            let mut batch = SubmitBatch::default();
            let stages = words32(m, record[2], waits)?;
            for (id, stage) in words64(m, record[0], waits)?.into_iter().zip(stages) {
                let semaphore = self.object(id, Kind::Semaphore)?;
                let state = semaphore_states.entry(id).or_insert(semaphore.state.get());
                // There is one queue. A wait consumes an earlier signal, even
                // if the driver has not completed it yet. Commit only on success.
                if *state != 1 {
                    return invalid();
                }
                *state = 0;
                batch.stages.push(compute_stage(stage)?);
                batch.waits.push(vk::Semaphore::from_raw(semaphore.raw));
                semaphores.push((id, semaphore));
            }
            for id in words64(m, record[3], count)? {
                let command = self.object(id, Kind::CommandBuffer)?;
                if command.state.get() != 2 || commands.iter().any(|c| Rc::ptr_eq(c, &command)) {
                    return invalid();
                }
                batch
                    .commands
                    .push(vk::CommandBuffer::from_raw(command.raw));
                commands.push(command);
            }
            for id in words64(m, record[5], signals)? {
                let semaphore = self.object(id, Kind::Semaphore)?;
                let state = semaphore_states.entry(id).or_insert(semaphore.state.get());
                if *state != 0 {
                    return invalid();
                }
                *state = 1;
                batch.signals.push(vk::Semaphore::from_raw(semaphore.raw));
                semaphores.push((id, semaphore));
            }
            batches.push(batch);
        }
        let d = &dev.native;
        let fence = if a[4] != 0 {
            let f = self.object(a[4], Kind::Fence)?;
            if unsafe { d.get_fence_status(vk::Fence::from_raw(f.raw)) }?
                || self.submissions.iter().any(|s| Rc::ptr_eq(&s.fence, &f))
            {
                return invalid();
            }
            f
        } else {
            let raw = unsafe { d.create_fence(&vk::FenceCreateInfo::default(), None) }?;
            Rc::new(Object {
                kind: Kind::Fence,
                raw: raw.as_raw(),
                device: dev.clone(),
                deps: RefCell::new(Vec::new()),
                bindings: RefCell::new(BTreeMap::new()),
                bound_buffers: RefCell::new(BTreeMap::new()),
                push_ranges: RefCell::new(Vec::new()),
                size: 0,
                flags: 0,
                state: Cell::new(0),
                mapped: Cell::new(0),
                budget: self.budget.clone(),
            })
        };
        let infos: Vec<_> = batches
            .iter()
            .map(|batch| {
                vk::SubmitInfo::default()
                    .wait_semaphores(&batch.waits)
                    .wait_dst_stage_mask(&batch.stages)
                    .command_buffers(&batch.commands)
                    .signal_semaphores(&batch.signals)
            })
            .collect();
        unsafe { d.queue_submit(self.queue, &infos, vk::Fence::from_raw(fence.raw)) }?;
        for c in &commands {
            c.state.set(3);
        }
        for (id, semaphore) in &semaphores {
            semaphore.state.set(semaphore_states[id]);
        }
        let mut resources = Vec::new();
        let mut pending = commands.clone();
        pending.extend(semaphores.into_iter().map(|(_, s)| s));
        while let Some(object) = pending.pop() {
            if resources.iter().any(|other| Rc::ptr_eq(other, &object)) {
                continue;
            }
            pending.extend(object.deps.borrow().iter().cloned());
            resources.push(object);
        }
        self.submissions.push(Submission {
            fence,
            commands,
            _resources: resources,
        });
        Ok(())
    }
    fn collect(&mut self) {
        self.reap_abandoned();
        self.retained.retain(|x| x.strong_count() != 0);
        self.submissions.retain(|s| {
            let done = unsafe {
                s.fence
                    .device
                    .native
                    .get_fence_status(vk::Fence::from_raw(s.fence.raw))
            } == Ok(true);
            if done {
                for command in &s.commands {
                    command.state.set(2);
                }
            }
            !done
        });
    }
    pub fn reap_abandoned(&mut self) {
        self.compiles
            .retain(|_, c| !c.abandoned || matches!(c.task.poll(), Ok(None)));
    }
    pub fn invoke(
        &mut self,
        memory: &mut Memory,
        operation: u64,
        address: u64,
        size: u64,
    ) -> std::result::Result<Invocation, MemoryError> {
        if size != 128 {
            return Ok(Invocation::Complete(
                vk::Result::ERROR_UNKNOWN.as_raw() as i64 as u64,
            ));
        }
        let mut bytes = [0; 128];
        memory.read(address, &mut bytes)?;
        let a: [u64; 16] = std::array::from_fn(|i| {
            u64::from_le_bytes(bytes[i * 8..i * 8 + 8].try_into().unwrap())
        });
        self.collect();
        let result = if operation == 27 {
            self.start_compile(memory, a).map(Invocation::Pending)
        } else {
            self.call(memory, operation, a)
                .map(|()| Invocation::Complete(0))
        };
        match result {
            Ok(value) => Ok(value),
            Err(Failure::Vk(e)) => Ok(Invocation::Complete(e.as_raw() as i64 as u64)),
            Err(Failure::Memory(e)) => Err(e),
        }
    }
    fn start_compile(&mut self, m: &Memory, a: [u64; 16]) -> Result<u64> {
        let device = self.device(a[0])?;
        self.room(1)?;
        output(m, a[4], 8)?;
        let layout = self.object(a[1], Kind::PipelineLayout)?;
        let shader = self.object(a[2], Kind::Shader)?;
        let name = std::ffi::CString::new(m.read_c_string(a[3], 256)?)
            .map_err(|_| Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
        let wire = words64(m, a[5], bounded(a[6], 64)? * 3)?;
        let data_size = bounded(a[8], 65536)?;
        let entries = specialization_entries(&wire, data_size)?;
        let mut data = vec![0; data_size];
        if data_size != 0 {
            m.read(a[7], &mut data)?;
        }
        let permit = self
            .budget
            .compile()
            .ok_or(Failure::Vk(vk::Result::ERROR_OUT_OF_HOST_MEMORY))?;
        let raw_shader = vk::ShaderModule::from_raw(shader.raw);
        let raw_layout = vk::PipelineLayout::from_raw(layout.raw);
        let task = crate::task::Task::spawn(move || {
            let _permit = permit;
            let specialization = vk::SpecializationInfo::default()
                .map_entries(&entries)
                .data(&data);
            let stage = vk::PipelineShaderStageCreateInfo::default()
                .stage(vk::ShaderStageFlags::COMPUTE)
                .module(raw_shader)
                .name(&name)
                .specialization_info(&specialization);
            let info = [vk::ComputePipelineCreateInfo::default()
                .stage(stage)
                .layout(raw_layout)];
            match unsafe {
                device
                    .native
                    .create_compute_pipelines(vk::PipelineCache::null(), &info, None)
            } {
                Ok(p) => Ok(CompiledPipeline { device, raw: p[0] }),
                Err((p, error)) => {
                    for raw in p {
                        unsafe {
                            device.native.destroy_pipeline(raw, None);
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
                layout,
                _shader: shader,
                output: a[4],
                abandoned: false,
            },
        );
        Ok(job)
    }
    pub fn abandon(&mut self, job: u64) {
        if let Some(c) = self.compiles.get_mut(&job) {
            c.abandoned = true;
        }
    }
    pub fn poll(
        &mut self,
        m: &mut Memory,
        job: u64,
    ) -> std::result::Result<Option<u64>, MemoryError> {
        let result = match self.compiles.get_mut(&job) {
            Some(c) => c.task.poll(),
            None => return Ok(Some(vk::Result::ERROR_DEVICE_LOST.as_raw() as i64 as u64)),
        };
        if matches!(result, Ok(None)) {
            return Ok(None);
        }
        let c = self.compiles.remove(&job).expect("polled compile");
        let mut pipeline = match result {
            Ok(Some(Ok(p))) => p,
            Ok(Some(Err(e))) => return Ok(Some(e.as_raw() as i64 as u64)),
            Err(()) => return Ok(Some(vk::Result::ERROR_DEVICE_LOST.as_raw() as i64 as u64)),
            Ok(None) => unreachable!(),
        };
        // Mappings may have changed while another CLONE_VM thread ran. Check
        // the live output before installing an ID; failure drops native output.
        output(m, c.output, 8).map_err(|e| match e {
            Failure::Memory(e) => e,
            _ => unreachable!(),
        })?;
        let id = self.put(
            Kind::Pipeline,
            pipeline.raw.as_raw(),
            vec![c.layout.clone()],
            0,
            0,
        );
        pipeline.raw = vk::Pipeline::null();
        m.write(c.output, &id.to_le_bytes())?;
        Ok(Some(0))
    }
    fn call(&mut self, m: &mut Memory, op: u64, a: [u64; 16]) -> Result<()> {
        if op == 1 {
            output(m, a[0], 8)?;
            if self.instance.is_some() {
                return Err(vk::Result::ERROR_OUT_OF_HOST_MEMORY.into());
            }
            let entry =
                load_entry().map_err(|_| Failure::Vk(vk::Result::ERROR_INITIALIZATION_FAILED))?;
            let extensions = unsafe { entry.enumerate_instance_extension_properties(None) }?;
            let portability=extensions.iter().any(|e| unsafe { std::ffi::CStr::from_ptr(e.extension_name.as_ptr()) }==ash::khr::portability_enumeration::NAME);
            let names = if portability {
                vec![ash::khr::portability_enumeration::NAME.as_ptr()]
            } else {
                Vec::new()
            };
            let app = vk::ApplicationInfo::default().api_version(vk::API_VERSION_1_0);
            let info = vk::InstanceCreateInfo::default()
                .application_info(&app)
                .enabled_extension_names(&names)
                .flags(if portability {
                    vk::InstanceCreateFlags::ENUMERATE_PORTABILITY_KHR
                } else {
                    vk::InstanceCreateFlags::empty()
                });
            let native = unsafe { entry.create_instance(&info, None) }?;
            let instance = Arc::new(Instance {
                native,
                _entry: entry,
            });
            let devices = unsafe { instance.native.enumerate_physical_devices() }?;
            self.physical = devices
                .into_iter()
                .find(|&p| {
                    unsafe {
                        instance
                            .native
                            .get_physical_device_queue_family_properties(p)
                    }
                    .iter()
                    .any(|q| q.queue_flags.contains(vk::QueueFlags::COMPUTE))
                })
                .ok_or(Failure::Vk(vk::Result::ERROR_INITIALIZATION_FAILED))?;
            self.instance = Some(instance);
            self.instance_id = self.id();
            self.physical_id = self.id();
            write(m, a[0], &self.instance_id.to_le_bytes())?;
            return Ok(());
        }
        if op == 2 {
            if a[0] != self.instance_id {
                return invalid();
            }
            self.clear_device();
            self.instance = None;
            self.instance_id = 0;
            self.physical_id = 0;
            return Ok(());
        }
        let instance = self
            .instance
            .clone()
            .ok_or(Failure::Vk(vk::Result::ERROR_INITIALIZATION_FAILED))?;
        if (3..=8).contains(&op)
            && a[0]
                != if op == 3 {
                    self.instance_id
                } else {
                    self.physical_id
                }
        {
            return invalid();
        }
        match op {
            3 => {
                output(m, a[1], 4)?;
                if a[2] != 0 {
                    output(m, a[2], 8)?;
                    write(m, a[2], &self.physical_id.to_le_bytes())?;
                }
                write(m, a[1], &1u32.to_le_bytes())?;
            }
            4 => {
                let mut b =
                    vec![0u64; std::mem::size_of::<vk::PhysicalDeviceProperties>().div_ceil(8)];
                output(m, a[1], std::mem::size_of::<vk::PhysicalDeviceProperties>())?;
                unsafe {
                    (instance.native.fp_v1_0().get_physical_device_properties)(
                        self.physical,
                        b.as_mut_ptr().cast(),
                    );
                }
                // This is an experimental 1.0 compute profile, not a complete
                // Vulkan conformance claim or the host driver's extension set.
                b[0] = (b[0] & 0xffffffff00000000) | vk::API_VERSION_1_0 as u64;
                write_words(
                    m,
                    a[1],
                    &b,
                    std::mem::size_of::<vk::PhysicalDeviceProperties>(),
                )?;
            }
            5 => {
                let props = unsafe {
                    instance
                        .native
                        .get_physical_device_memory_properties(self.physical)
                };
                let mut b = vec![
                    0u64;
                    std::mem::size_of::<vk::PhysicalDeviceMemoryProperties>()
                        .div_ceil(8)
                ];
                output(
                    m,
                    a[1],
                    std::mem::size_of::<vk::PhysicalDeviceMemoryProperties>(),
                )?;
                unsafe {
                    (instance
                        .native
                        .fp_v1_0()
                        .get_physical_device_memory_properties)(
                        self.physical,
                        b.as_mut_ptr().cast(),
                    );
                }
                // Shadow mappings require explicit flush/invalidate. Never
                // falsely advertise HOST_COHERENT while CPU/GPU storage differs.
                for i in 0..props.memory_type_count as usize {
                    let at = 4 + i * 8;
                    let word = at / 8;
                    let shift = (at % 8) * 8;
                    b[word] &= !((vk::MemoryPropertyFlags::HOST_COHERENT.as_raw() as u64) << shift);
                }
                write_words(
                    m,
                    a[1],
                    &b,
                    std::mem::size_of::<vk::PhysicalDeviceMemoryProperties>(),
                )?;
            }
            6 => {
                let props = unsafe {
                    instance
                        .native
                        .get_physical_device_queue_family_properties(self.physical)
                };
                let capacity = read32(m, a[1])? as usize;
                output(m, a[1], 4)?;
                if a[2] != 0 {
                    output(m, a[2], capacity.min(props.len()) * 24)?;
                    for (i, q) in props.iter().take(capacity).enumerate() {
                        let mut v = Vec::new();
                        for n in [
                            q.queue_flags.as_raw() & !vk::QueueFlags::GRAPHICS.as_raw(),
                            q.queue_count.min(1),
                            q.timestamp_valid_bits,
                            q.min_image_transfer_granularity.width,
                            q.min_image_transfer_granularity.height,
                            q.min_image_transfer_granularity.depth,
                        ] {
                            v.extend(n.to_le_bytes());
                        }
                        write(m, a[2] + i as u64 * 24, &v)?;
                    }
                }
                write(
                    m,
                    a[1],
                    &(if a[2] == 0 {
                        props.len()
                    } else {
                        capacity.min(props.len())
                    } as u32)
                        .to_le_bytes(),
                )?;
            }
            7 => {
                let zero = vec![0; std::mem::size_of::<vk::PhysicalDeviceFeatures>()];
                write(m, a[1], &zero)?;
            }
            8 => {
                output(m, a[2], 8)?;
                if self.device.is_some() {
                    return Err(vk::Result::ERROR_TOO_MANY_OBJECTS.into());
                }
                let queues = unsafe {
                    instance
                        .native
                        .get_physical_device_queue_family_properties(self.physical)
                };
                if !queues
                    .get(a[1] as usize)
                    .is_some_and(|q| q.queue_flags.contains(vk::QueueFlags::COMPUTE))
                {
                    return unsupported();
                }
                let priorities = [1.0];
                let queue = [vk::DeviceQueueCreateInfo::default()
                    .queue_family_index(a[1] as u32)
                    .queue_priorities(&priorities)];
                let ext = unsafe {
                    instance
                        .native
                        .enumerate_device_extension_properties(self.physical)
                }?;
                let subset=ext.iter().any(|e| unsafe { std::ffi::CStr::from_ptr(e.extension_name.as_ptr()) }==ash::khr::portability_subset::NAME);
                let names = if subset {
                    vec![ash::khr::portability_subset::NAME.as_ptr()]
                } else {
                    Vec::new()
                };
                let info = vk::DeviceCreateInfo::default()
                    .queue_create_infos(&queue)
                    .enabled_extension_names(&names);
                let native = unsafe { instance.native.create_device(self.physical, &info, None) }?;
                self.queue = unsafe { native.get_device_queue(a[1] as u32, 0) };
                self.family = a[1] as u32;
                let limits = unsafe {
                    instance
                        .native
                        .get_physical_device_properties(self.physical)
                }
                .limits;
                self.device = Some(Arc::new(Device {
                    native,
                    instance,
                    atom: limits.non_coherent_atom_size,
                    map_alignment: limits.min_memory_map_alignment as u64,
                }));
                self.device_id = self.id();
                self.queue_id = self.id();
                write(m, a[2], &self.device_id.to_le_bytes())?;
            }
            9 => {
                self.device(a[0])?;
                self.clear_device();
            }
            10 => {
                self.device(a[0])?;
                if a[1] != self.family as u64 || a[2] != 0 {
                    return invalid();
                }
                write(m, a[3], &self.queue_id.to_le_bytes())?;
            }
            _ => {
                self.device(a[0])?;
                self.device_call(m, op, a)?;
            }
        }
        Ok(())
    }
    fn clear_device(&mut self) {
        self.compiles.clear();
        if let Some(d) = &self.device {
            unsafe {
                let _ = d.native.device_wait_idle();
            }
        }
        self.submissions.clear();
        self.mappings.clear();
        self.objects.clear();
        self.device = None;
        self.device_id = 0;
        self.queue_id = 0;
    }
    fn device_call(&mut self, m: &mut Memory, op: u64, a: [u64; 16]) -> Result<()> {
        let dev = self.device.as_ref().unwrap().clone();
        let d = &dev.native;
        match op {
            11 => {
                self.room(1)?;
                output(m, a[3], 8)?;
                if a[1] == 0 || a[1] > MAX_ALLOCATION {
                    return Err(vk::Result::ERROR_OUT_OF_DEVICE_MEMORY.into());
                }
                let allowed = vk::BufferUsageFlags::STORAGE_BUFFER
                    | vk::BufferUsageFlags::UNIFORM_BUFFER
                    | vk::BufferUsageFlags::TRANSFER_SRC
                    | vk::BufferUsageFlags::TRANSFER_DST;
                if a[2] as u32 & !allowed.as_raw() != 0 {
                    return unsupported();
                }
                let info = vk::BufferCreateInfo::default()
                    .size(a[1])
                    .usage(vk::BufferUsageFlags::from_raw(a[2] as u32));
                let raw = unsafe { d.create_buffer(&info, None) }?;
                let id = self.put(Kind::Buffer, raw.as_raw(), vec![], a[1], a[2] as u32);
                write(m, a[3], &id.to_le_bytes())?;
            }
            12 => self.remove(a[1], Kind::Buffer)?,
            13 => {
                let b = self.object(a[1], Kind::Buffer)?;
                let r = unsafe { d.get_buffer_memory_requirements(vk::Buffer::from_raw(b.raw)) };
                let mut bytes = Vec::new();
                bytes.extend(r.size.to_le_bytes());
                bytes.extend(r.alignment.to_le_bytes());
                bytes.extend(r.memory_type_bits.to_le_bytes());
                bytes.extend([0; 4]);
                write(m, a[2], &bytes)?;
            }
            14 => {
                self.room(1)?;
                output(m, a[3], 8)?;
                if a[1] == 0 || a[1] > MAX_ALLOCATION {
                    return Err(vk::Result::ERROR_OUT_OF_DEVICE_MEMORY.into());
                }
                let retained_bytes = self
                    .retained
                    .iter()
                    .filter_map(Weak::upgrade)
                    .filter(|x| x.kind == Kind::Memory)
                    .map(|x| x.size)
                    .sum::<u64>();
                if retained_bytes
                    .checked_add(a[1])
                    .is_none_or(|n| n > 128 << 20)
                {
                    return Err(vk::Result::ERROR_OUT_OF_DEVICE_MEMORY.into());
                }
                let props = unsafe {
                    dev.instance
                        .native
                        .get_physical_device_memory_properties(self.physical)
                };
                if a[2] >= props.memory_type_count as u64 {
                    return invalid();
                }
                let info = vk::MemoryAllocateInfo::default()
                    .allocation_size(a[1])
                    .memory_type_index(a[2] as u32);
                if !self.budget.reserve(a[1]) {
                    return Err(vk::Result::ERROR_OUT_OF_DEVICE_MEMORY.into());
                }
                let raw = match unsafe { d.allocate_memory(&info, None) } {
                    Ok(raw) => raw,
                    Err(e) => {
                        self.budget.release(a[1]);
                        return Err(e.into());
                    }
                };
                let id = self.put(
                    Kind::Memory,
                    raw.as_raw(),
                    vec![],
                    a[1],
                    props.memory_types[a[2] as usize].property_flags.as_raw(),
                );
                self.objects[&id].state.set(a[2] as u8);
                write(m, a[3], &id.to_le_bytes())?;
            }
            15 => self.remove(a[1], Kind::Memory)?,
            16 => {
                let b = self.object(a[1], Kind::Buffer)?;
                let mem = self.object(a[2], Kind::Memory)?;
                let r = unsafe { d.get_buffer_memory_requirements(vk::Buffer::from_raw(b.raw)) };
                if !a[3].is_multiple_of(r.alignment)
                    || r.memory_type_bits & (1u32 << mem.state.get()) == 0
                    || a[3].checked_add(r.size).is_none_or(|n| n > mem.size)
                    || !b.deps.borrow().is_empty()
                {
                    return invalid();
                }
                unsafe {
                    d.bind_buffer_memory(
                        vk::Buffer::from_raw(b.raw),
                        vk::DeviceMemory::from_raw(mem.raw),
                        a[3],
                    )
                }?;
                b.retain(mem);
            }
            17 => {
                let mem = self.object(a[1], Kind::Memory)?;
                if mem.mapped.get() != 0
                    || mem.flags & vk::MemoryPropertyFlags::HOST_VISIBLE.as_raw() == 0
                {
                    return invalid();
                }
                let len = if a[3] == u64::MAX {
                    mem.size.saturating_sub(a[2])
                } else {
                    a[3]
                };
                if len == 0 || a[2].checked_add(len).is_none_or(|n| n > mem.size) {
                    return invalid();
                }
                output(m, a[4], len as usize)?;
                let ptr = unsafe {
                    d.map_memory(
                        vk::DeviceMemory::from_raw(mem.raw),
                        0,
                        vk::WHOLE_SIZE,
                        vk::MemoryMapFlags::empty(),
                    )
                }?;
                mem.mapped.set(ptr as usize);
                self.mappings.insert(
                    a[1],
                    Mapping {
                        memory: mem,
                        guest: a[4],
                        offset: a[2],
                        size: len,
                    },
                );
            }
            18 => {
                let map = self
                    .mappings
                    .remove(&a[1])
                    .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
                unsafe {
                    d.unmap_memory(vk::DeviceMemory::from_raw(map.memory.raw));
                }
                map.memory.mapped.set(0);
            }
            19 | 20 => {
                let map = self
                    .mappings
                    .get(&a[1])
                    .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
                let len = if a[3] == u64::MAX {
                    map.memory.size.saturating_sub(a[2])
                } else {
                    a[3]
                };
                let atom = dev.atom;
                if !a[2].is_multiple_of(atom)
                    || a[3] != u64::MAX
                        && !len.is_multiple_of(atom)
                        && a[2].checked_add(len) != Some(map.memory.size)
                {
                    return invalid();
                }
                if a[2] < map.offset
                    || a[2]
                        .checked_add(len)
                        .is_none_or(|n| n > map.offset + map.size)
                {
                    return invalid();
                }
                let guest = map.guest + (a[2] - map.offset);
                let ptr = (map.memory.mapped.get() as *mut u8).wrapping_add(a[2] as usize);
                // Native mapped storage is retained by Mapping. No Rust slice
                // or guest-memory guard is kept while the GPU runs.
                let range = [vk::MappedMemoryRange::default()
                    .memory(vk::DeviceMemory::from_raw(map.memory.raw))
                    .offset(a[2])
                    .size(a[3])];
                if op == 19 {
                    // One guest-to-native copy per page, with no full-range Vec.
                    let transfer = unsafe { transfer_pages(m, guest, ptr, len as usize, true) }?;
                    self.transfers.upload_bytes += len;
                    self.transfers.direct_bytes += transfer.direct_bytes;
                    self.transfers.buffered_bytes += transfer.buffered_bytes;
                    unsafe {
                        d.flush_mapped_memory_ranges(&range)?;
                    }
                } else {
                    output(m, guest, len as usize)?;
                    unsafe {
                        d.invalidate_mapped_memory_ranges(&range)?;
                    }
                    let transfer = unsafe { transfer_pages(m, guest, ptr, len as usize, false) }?;
                    self.transfers.download_bytes += len;
                    self.transfers.direct_bytes += transfer.direct_bytes;
                    self.transfers.buffered_bytes += transfer.buffered_bytes;
                }
            }
            21 => {
                self.room(1)?;
                output(m, a[3], 8)?;
                if a[2] == 0 || a[2] > 1 << 20 || !a[2].is_multiple_of(4) {
                    return invalid();
                }
                let code = words32(m, a[1], a[2] as usize / 4)?;
                if code.first() != Some(&0x07230203) {
                    return invalid();
                }
                let info = vk::ShaderModuleCreateInfo::default().code(&code);
                let raw = unsafe { d.create_shader_module(&info, None) }?;
                let id = self.put(Kind::Shader, raw.as_raw(), vec![], 0, 0);
                write(m, a[3], &id.to_le_bytes())?;
            }
            22 => self.remove(a[1], Kind::Shader)?,
            23 => {
                self.room(1)?;
                output(m, a[3], 8)?;
                let wire = words64(m, a[1], bounded(a[2], 64)? * 4)?;
                let mut bindings = Vec::new();
                let mut metadata = BTreeMap::new();
                for b in wire.chunks_exact(4) {
                    if b[1] != 6 && b[1] != 7
                        || b[2] == 0
                        || b[2] > 64
                        || b[3] != vk::ShaderStageFlags::COMPUTE.as_raw() as u64
                    {
                        return unsupported();
                    }
                    if b[0] > u32::MAX as u64
                        || metadata
                            .insert(b[0] as u32, (b[1] as u32, b[2] as u32))
                            .is_some()
                    {
                        return invalid();
                    }
                    bindings.push(
                        vk::DescriptorSetLayoutBinding::default()
                            .binding(b[0] as u32)
                            .descriptor_type(vk::DescriptorType::from_raw(b[1] as i32))
                            .descriptor_count(b[2] as u32)
                            .stage_flags(vk::ShaderStageFlags::COMPUTE),
                    );
                }
                let info = vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings);
                let raw = unsafe { d.create_descriptor_set_layout(&info, None) }?;
                let id = self.put(Kind::DescriptorLayout, raw.as_raw(), vec![], 0, 0);
                *self.objects[&id].bindings.borrow_mut() = metadata;
                write(m, a[3], &id.to_le_bytes())?;
            }
            24 => self.remove(a[1], Kind::DescriptorLayout)?,
            25 => {
                self.room(1)?;
                output(m, a[5], 8)?;
                let ids = words64(m, a[1], bounded(a[2], 8)?)?;
                let mut deps = Vec::new();
                let mut layouts = Vec::new();
                for id in ids {
                    let obj = self.object(id, Kind::DescriptorLayout)?;
                    layouts.push(vk::DescriptorSetLayout::from_raw(obj.raw));
                    deps.push(obj);
                }
                let wire = words64(m, a[3], bounded(a[4], 8)? * 3)?;
                let mut ranges = Vec::new();
                for r in wire.chunks_exact(3) {
                    if r[0] != 32 || r[1] % 4 != 0 || r[2] % 4 != 0 || r[1] + r[2] > 128 {
                        return unsupported();
                    }
                    ranges.push(
                        vk::PushConstantRange::default()
                            .stage_flags(vk::ShaderStageFlags::COMPUTE)
                            .offset(r[1] as u32)
                            .size(r[2] as u32),
                    );
                }
                let info = vk::PipelineLayoutCreateInfo::default()
                    .set_layouts(&layouts)
                    .push_constant_ranges(&ranges);
                let raw = unsafe { d.create_pipeline_layout(&info, None) }?;
                let id = self.put(Kind::PipelineLayout, raw.as_raw(), deps, 0, 0);
                *self.objects[&id].push_ranges.borrow_mut() =
                    ranges.iter().map(|r| (r.offset, r.size)).collect();
                write(m, a[5], &id.to_le_bytes())?;
            }
            26 => self.remove(a[1], Kind::PipelineLayout)?,
            28 => self.remove(a[1], Kind::Pipeline)?,
            29 => {
                self.room(1)?;
                output(m, a[5], 8)?;
                let wire = words64(m, a[1], bounded(a[2], 64)? * 2)?;
                let mut sizes = Vec::new();
                for x in wire.chunks_exact(2) {
                    if x[0] != 6 && x[0] != 7 || x[1] == 0 || x[1] > 256 {
                        return unsupported();
                    }
                    sizes.push(
                        vk::DescriptorPoolSize::default()
                            .ty(vk::DescriptorType::from_raw(x[0] as i32))
                            .descriptor_count(x[1] as u32),
                    );
                }
                if a[3] == 0 || a[3] > 64 || a[4] & !1 != 0 {
                    return unsupported();
                }
                let info = vk::DescriptorPoolCreateInfo::default()
                    .pool_sizes(&sizes)
                    .max_sets(a[3] as u32)
                    .flags(vk::DescriptorPoolCreateFlags::FREE_DESCRIPTOR_SET);
                let raw = unsafe { d.create_descriptor_pool(&info, None) }?;
                let id = self.put(Kind::DescriptorPool, raw.as_raw(), vec![], 0, a[4] as u32);
                write(m, a[5], &id.to_le_bytes())?;
            }
            30 => self.remove(a[1], Kind::DescriptorPool)?,
            31 => {
                let count = bounded(a[3], 64)?;
                self.room(count)?;
                output(m, a[4], count * 8)?;
                let pool = self.object(a[1], Kind::DescriptorPool)?;
                let ids = words64(m, a[2], count)?;
                let mut deps = Vec::new();
                let mut layouts = Vec::new();
                for id in ids {
                    let obj = self.object(id, Kind::DescriptorLayout)?;
                    layouts.push(vk::DescriptorSetLayout::from_raw(obj.raw));
                    deps.push(obj);
                }
                let info = vk::DescriptorSetAllocateInfo::default()
                    .descriptor_pool(vk::DescriptorPool::from_raw(pool.raw))
                    .set_layouts(&layouts);
                let raw = unsafe { d.allocate_descriptor_sets(&info) }?;
                for (i, (set, layout)) in raw.into_iter().zip(deps).enumerate() {
                    let id = self.put(
                        Kind::DescriptorSet,
                        set.as_raw(),
                        vec![pool.clone(), layout],
                        0,
                        0,
                    );
                    write(m, a[4] + i as u64 * 8, &id.to_le_bytes())?;
                }
            }
            32 => {
                let pool = self.object(a[1], Kind::DescriptorPool)?;
                if pool.flags & 1 == 0 {
                    return invalid();
                }
                let ids = words64(m, a[2], bounded(a[3], 64)?)?;
                for id in &ids {
                    let set = self.object(*id, Kind::DescriptorSet)?;
                    if !Rc::ptr_eq(&set.deps.borrow()[0], &pool) {
                        return invalid();
                    }
                }
                for id in ids {
                    self.remove(id, Kind::DescriptorSet)?;
                }
            }
            33 => {
                let set = self.object(a[1], Kind::DescriptorSet)?;
                if a[5] != 6 && a[5] != 7 {
                    return unsupported();
                }
                let layout = set.deps.borrow()[1].clone();
                let shape = layout
                    .bindings
                    .borrow()
                    .get(&(a[2] as u32))
                    .copied()
                    .ok_or(Failure::Vk(vk::Result::ERROR_UNKNOWN))?;
                if shape.0 as u64 != a[5]
                    || a[3].checked_add(a[4]).is_none_or(|n| n > shape.1 as u64)
                {
                    return invalid();
                }
                let wire = words64(m, a[6], bounded(a[4], 64)? * 3)?;
                let mut buffers = Vec::new();
                let mut deps = Vec::new();
                for x in wire.chunks_exact(3) {
                    let b = self.object(x[0], Kind::Buffer)?;
                    let usage = if a[5] == 6 {
                        vk::BufferUsageFlags::UNIFORM_BUFFER
                    } else {
                        vk::BufferUsageFlags::STORAGE_BUFFER
                    };
                    if b.flags & usage.as_raw() == 0 {
                        return invalid();
                    }
                    if b.deps.borrow().is_empty()
                        || x[1] >= b.size
                        || x[2] != u64::MAX && x[1].checked_add(x[2]).is_none_or(|n| n > b.size)
                    {
                        return invalid();
                    }
                    buffers.push(
                        vk::DescriptorBufferInfo::default()
                            .buffer(vk::Buffer::from_raw(b.raw))
                            .offset(x[1])
                            .range(x[2]),
                    );
                    deps.push(b);
                }
                let writes = [vk::WriteDescriptorSet::default()
                    .dst_set(vk::DescriptorSet::from_raw(set.raw))
                    .dst_binding(a[2] as u32)
                    .dst_array_element(a[3] as u32)
                    .descriptor_type(vk::DescriptorType::from_raw(a[5] as i32))
                    .buffer_info(&buffers)];
                unsafe {
                    d.update_descriptor_sets(&writes, &[]);
                }
                set.deps.borrow_mut().truncate(2);
                for (i, b) in deps.into_iter().enumerate() {
                    set.bound_buffers
                        .borrow_mut()
                        .insert((a[2] as u32, a[3] as u32 + i as u32), b);
                }
                for b in set.bound_buffers.borrow().values() {
                    set.retain(b.clone());
                }
            }
            34 => {
                self.room(1)?;
                output(m, a[3], 8)?;
                if a[1] != self.family as u64 || a[2] & !3 != 0 {
                    return unsupported();
                }
                let info = vk::CommandPoolCreateInfo::default()
                    .queue_family_index(self.family)
                    .flags(vk::CommandPoolCreateFlags::from_raw(a[2] as u32));
                let raw = unsafe { d.create_command_pool(&info, None) }?;
                let id = self.put(Kind::CommandPool, raw.as_raw(), vec![], 0, a[2] as u32);
                write(m, a[3], &id.to_le_bytes())?;
            }
            35 => self.remove(a[1], Kind::CommandPool)?,
            36 => {
                self.room(1)?;
                output(m, a[2], 8)?;
                let pool = self.object(a[1], Kind::CommandPool)?;
                let info = vk::CommandBufferAllocateInfo::default()
                    .command_pool(vk::CommandPool::from_raw(pool.raw))
                    .level(vk::CommandBufferLevel::PRIMARY)
                    .command_buffer_count(1);
                let raw = unsafe { d.allocate_command_buffers(&info) }?[0];
                let id = self.put(Kind::CommandBuffer, raw.as_raw(), vec![pool], 0, 0);
                write(m, a[2], &id.to_le_bytes())?;
            }
            37 => {
                let c = self.object(a[2], Kind::CommandBuffer)?;
                let pool = self.object(a[1], Kind::CommandPool)?;
                if !Rc::ptr_eq(&c.deps.borrow()[0], &pool) {
                    return invalid();
                }
                self.remove(a[2], Kind::CommandBuffer)?;
            }
            38..=40 => {
                let c = self.object(a[1], Kind::CommandBuffer)?;
                if c.state.get() == 3 {
                    return invalid();
                }
                let raw = vk::CommandBuffer::from_raw(c.raw);
                unsafe {
                    if op == 38 {
                        if a[2] & !1 != 0 {
                            return unsupported();
                        }
                        let info = vk::CommandBufferBeginInfo::default()
                            .flags(vk::CommandBufferUsageFlags::from_raw(a[2] as u32));
                        d.begin_command_buffer(raw, &info)?;
                        c.deps.borrow_mut().truncate(1);
                        c.state.set(1);
                    } else if op == 39 {
                        if c.state.get() != 1 {
                            return invalid();
                        }
                        d.end_command_buffer(raw)?;
                        c.state.set(2);
                    } else {
                        if c.deps.borrow()[0].flags & 2 == 0 || a[2] & !1 != 0 {
                            return invalid();
                        }
                        d.reset_command_buffer(
                            raw,
                            vk::CommandBufferResetFlags::from_raw(a[2] as u32),
                        )?;
                        c.deps.borrow_mut().truncate(1);
                        c.state.set(0);
                    }
                }
            }
            41..=47 => {
                let c = self.object(a[1], Kind::CommandBuffer)?;
                if c.state.get() != 1 {
                    return invalid();
                }
                let raw = vk::CommandBuffer::from_raw(c.raw);
                match op {
                    41 => {
                        let p = self.object(a[2], Kind::Pipeline)?;
                        unsafe {
                            d.cmd_bind_pipeline(
                                raw,
                                vk::PipelineBindPoint::COMPUTE,
                                vk::Pipeline::from_raw(p.raw),
                            );
                        }
                        c.retain(p);
                    }
                    42 => {
                        let layout = self.object(a[2], Kind::PipelineLayout)?;
                        if a[3]
                            .checked_add(a[5])
                            .is_none_or(|n| n > layout.deps.borrow().len() as u64)
                        {
                            return invalid();
                        }
                        let ids = words64(m, a[4], bounded(a[5], 8)?)?;
                        let mut sets = Vec::new();
                        for (i, id) in ids.into_iter().enumerate() {
                            let s = self.object(id, Kind::DescriptorSet)?;
                            let expected = layout.deps.borrow()[a[3] as usize + i].clone();
                            let actual = s.deps.borrow()[1].clone();
                            if *expected.bindings.borrow() != *actual.bindings.borrow() {
                                return invalid();
                            }
                            sets.push(vk::DescriptorSet::from_raw(s.raw));
                            c.retain(s);
                        }
                        unsafe {
                            d.cmd_bind_descriptor_sets(
                                raw,
                                vk::PipelineBindPoint::COMPUTE,
                                vk::PipelineLayout::from_raw(layout.raw),
                                a[3] as u32,
                                &sets,
                                &[],
                            );
                        }
                        c.retain(layout);
                    }
                    43 => {
                        let lim = unsafe {
                            dev.instance
                                .native
                                .get_physical_device_properties(self.physical)
                        }
                        .limits
                        .max_compute_work_group_count;
                        if (0..3).any(|i| a[2 + i] > lim[i] as u64) {
                            return invalid();
                        }
                        unsafe {
                            d.cmd_dispatch(raw, a[2] as u32, a[3] as u32, a[4] as u32);
                        }
                    }
                    44 => {
                        let barriers = [vk::MemoryBarrier::default()
                            .src_access_mask(vk::AccessFlags::from_raw(a[4] as u32))
                            .dst_access_mask(vk::AccessFlags::from_raw(a[5] as u32))];
                        unsafe {
                            d.cmd_pipeline_barrier(
                                raw,
                                vk::PipelineStageFlags::from_raw(a[2] as u32),
                                vk::PipelineStageFlags::from_raw(a[3] as u32),
                                vk::DependencyFlags::empty(),
                                &barriers,
                                &[],
                                &[],
                            );
                        }
                    }
                    45 => {
                        let src = self.object(a[2], Kind::Buffer)?;
                        let dst = self.object(a[3], Kind::Buffer)?;
                        if src.deps.borrow().is_empty()
                            || dst.deps.borrow().is_empty()
                            || src.flags & vk::BufferUsageFlags::TRANSFER_SRC.as_raw() == 0
                            || dst.flags & vk::BufferUsageFlags::TRANSFER_DST.as_raw() == 0
                        {
                            return invalid();
                        }
                        let wire = words64(m, a[4], bounded(a[5], 64)? * 3)?;
                        let mut regions = Vec::new();
                        for x in wire.chunks_exact(3) {
                            if x[2] == 0
                                || x[0].checked_add(x[2]).is_none_or(|n| n > src.size)
                                || x[1].checked_add(x[2]).is_none_or(|n| n > dst.size)
                            {
                                return invalid();
                            }
                            regions.push(
                                vk::BufferCopy::default()
                                    .src_offset(x[0])
                                    .dst_offset(x[1])
                                    .size(x[2]),
                            );
                        }
                        unsafe {
                            d.cmd_copy_buffer(
                                raw,
                                vk::Buffer::from_raw(src.raw),
                                vk::Buffer::from_raw(dst.raw),
                                &regions,
                            );
                        }
                        c.retain(src);
                        c.retain(dst);
                    }
                    46 => {
                        let b = self.object(a[2], Kind::Buffer)?;
                        if b.deps.borrow().is_empty()
                            || b.flags & vk::BufferUsageFlags::TRANSFER_DST.as_raw() == 0
                        {
                            return invalid();
                        }
                        let len = if a[4] == u64::MAX {
                            b.size.saturating_sub(a[3])
                        } else {
                            a[4]
                        };
                        if !a[3].is_multiple_of(4)
                            || len % 4 != 0
                            || a[3].checked_add(len).is_none_or(|n| n > b.size)
                        {
                            return invalid();
                        }
                        unsafe {
                            d.cmd_fill_buffer(
                                raw,
                                vk::Buffer::from_raw(b.raw),
                                a[3],
                                a[4],
                                a[5] as u32,
                            );
                        }
                        c.retain(b);
                    }
                    47 => {
                        let layout = self.object(a[2], Kind::PipelineLayout)?;
                        if !layout.push_ranges.borrow().iter().any(|&(offset, size)| {
                            a[3] >= offset as u64
                                && a[3]
                                    .checked_add(a[4])
                                    .is_some_and(|end| end <= (offset as u64 + size as u64))
                        }) {
                            return invalid();
                        }
                        if !a[3].is_multiple_of(4)
                            || !a[4].is_multiple_of(4)
                            || a[3].checked_add(a[4]).is_none_or(|n| n > 128)
                        {
                            return invalid();
                        }
                        let mut bytes = vec![0; a[4] as usize];
                        m.read(a[5], &mut bytes)?;
                        unsafe {
                            d.cmd_push_constants(
                                raw,
                                vk::PipelineLayout::from_raw(layout.raw),
                                vk::ShaderStageFlags::COMPUTE,
                                a[3] as u32,
                                &bytes,
                            );
                        }
                        c.retain(layout);
                    }
                    _ => unreachable!(),
                }
            }
            48 => {
                self.room(1)?;
                output(m, a[2], 8)?;
                if a[1] & !1 != 0 {
                    return invalid();
                }
                let info = vk::FenceCreateInfo::default()
                    .flags(vk::FenceCreateFlags::from_raw(a[1] as u32));
                let raw = unsafe { d.create_fence(&info, None) }?;
                let id = self.put(Kind::Fence, raw.as_raw(), vec![], 0, 0);
                write(m, a[2], &id.to_le_bytes())?;
            }
            49 => self.remove(a[1], Kind::Fence)?,
            50 => {
                let f = self.object(a[1], Kind::Fence)?;
                if self.submissions.iter().any(|s| Rc::ptr_eq(&s.fence, &f)) {
                    return invalid();
                }
                unsafe { d.reset_fences(&[vk::Fence::from_raw(f.raw)]) }?;
            }
            51 => {
                let f = self.object(a[1], Kind::Fence)?;
                if !unsafe { d.get_fence_status(vk::Fence::from_raw(f.raw)) }? {
                    return Err(vk::Result::NOT_READY.into());
                }
            }
            52 | 56 => self.submit(m, op, a, dev.clone())?,
            54 => {
                self.room(1)?;
                output(m, a[1], 8)?;
                let raw = unsafe { d.create_semaphore(&vk::SemaphoreCreateInfo::default(), None) }?;
                let id = self.put(Kind::Semaphore, raw.as_raw(), vec![], 0, 0);
                write(m, a[1], &id.to_le_bytes())?;
            }
            55 => self.remove(a[1], Kind::Semaphore)?,
            57 => {
                let alignment = dev.map_alignment;
                write(m, a[1], &alignment.to_le_bytes())?;
            }
            53 => {
                let pool = self.object(a[1], Kind::CommandPool)?;
                let children: Vec<_> = self
                    .objects
                    .values()
                    .filter(|x| {
                        x.kind == Kind::CommandBuffer
                            && x.deps
                                .borrow()
                                .first()
                                .is_some_and(|p| Rc::ptr_eq(p, &pool))
                    })
                    .cloned()
                    .collect();
                if children.iter().any(|c| c.state.get() == 3) || a[2] & !1 != 0 {
                    return invalid();
                }
                unsafe {
                    d.reset_command_pool(
                        vk::CommandPool::from_raw(pool.raw),
                        vk::CommandPoolResetFlags::from_raw(a[2] as u32),
                    )
                }?;
                for c in children {
                    c.state.set(0);
                    c.deps.borrow_mut().truncate(1);
                }
            }
            _ => return unsupported(),
        }
        Ok(())
    }
}
impl Drop for Backend {
    fn drop(&mut self) {
        self.clear_device();
    }
}
#[cfg(trames_moltenvk)]
fn load_entry() -> std::result::Result<Entry, ash::LoadingError> {
    unsafe extern "system" {
        fn vkGetInstanceProcAddr(
            instance: vk::Instance,
            name: *const std::ffi::c_char,
        ) -> vk::PFN_vkVoidFunction;
    }
    Ok(unsafe {
        Entry::from_static_fn(ash::StaticFn {
            get_instance_proc_addr: vkGetInstanceProcAddr,
        })
    })
}
#[cfg(not(trames_moltenvk))]
fn load_entry() -> std::result::Result<Entry, ash::LoadingError> {
    if let Some(path) = std::env::var_os("TRAMES_VULKAN_LIBRARY") {
        unsafe { Entry::load_from(path) }
    } else {
        unsafe { Entry::load() }
    }
}
fn bounded(n: u64, limit: usize) -> Result<usize> {
    if n > limit as u64 {
        unsupported()
    } else {
        Ok(n as usize)
    }
}
/// Native pointer covers a retained, mapped driver allocation disjoint from
/// guest backing. Only this callback borrows guest pages; nothing escapes it.
unsafe fn transfer_pages(
    m: &mut Memory,
    guest: u64,
    native: *mut u8,
    len: usize,
    upload: bool,
) -> std::result::Result<TransferMetrics, MemoryError> {
    m.check(
        guest,
        len,
        if upload { Access::Read } else { Access::Write },
    )?;
    let mut transferred = TransferMetrics::default();
    let mut offset = 0;
    while offset < len {
        let address = guest + offset as u64;
        let n = (len - offset)
            .min((trames_memory::PAGE_SIZE - address % trames_memory::PAGE_SIZE) as usize);
        let direct = if upload {
            m.with_read_chunk(address, n, |bytes| unsafe {
                std::ptr::copy_nonoverlapping(bytes.as_ptr(), native.add(offset), n);
            })?
            .1
        } else {
            m.with_write_chunk(address, n, |bytes| unsafe {
                std::ptr::copy_nonoverlapping(native.add(offset), bytes.as_mut_ptr(), n);
            })?
            .1
        };
        if direct {
            transferred.direct_bytes += n as u64;
        } else {
            transferred.buffered_bytes += n as u64;
        }
        offset += n;
    }
    Ok(transferred)
}
fn compute_stage(stage: u32) -> Result<vk::PipelineStageFlags> {
    let allowed = vk::PipelineStageFlags::TOP_OF_PIPE
        | vk::PipelineStageFlags::COMPUTE_SHADER
        | vk::PipelineStageFlags::TRANSFER
        | vk::PipelineStageFlags::BOTTOM_OF_PIPE
        | vk::PipelineStageFlags::HOST
        | vk::PipelineStageFlags::ALL_COMMANDS;
    if stage == 0 || stage & !allowed.as_raw() != 0 {
        return unsupported();
    }
    Ok(vk::PipelineStageFlags::from_raw(stage))
}
fn specialization_entries(wire: &[u64], size: usize) -> Result<Vec<vk::SpecializationMapEntry>> {
    let mut entries = Vec::new();
    for e in wire.chunks_exact(3) {
        if e[0] > u32::MAX as u64
            || e[1] > u32::MAX as u64
            || e[2] == 0
            || e[1].checked_add(e[2]).is_none_or(|end| end > size as u64)
            || entries
                .iter()
                .any(|entry: &vk::SpecializationMapEntry| entry.constant_id == e[0] as u32)
        {
            return invalid();
        }
        entries.push(vk::SpecializationMapEntry {
            constant_id: e[0] as u32,
            offset: e[1] as u32,
            size: e[2] as usize,
        });
    }
    Ok(entries)
}
fn output(m: &Memory, p: u64, n: usize) -> Result<()> {
    m.check(p, n, Access::Write)?;
    Ok(())
}
fn write(m: &mut Memory, p: u64, b: &[u8]) -> Result<()> {
    m.write(p, b)?;
    Ok(())
}
fn read32(m: &Memory, p: u64) -> Result<u32> {
    let mut b = [0; 4];
    m.read(p, &mut b)?;
    Ok(u32::from_le_bytes(b))
}
fn words64(m: &Memory, p: u64, n: usize) -> Result<Vec<u64>> {
    let mut b = vec![0; n * 8];
    m.read(p, &mut b)?;
    Ok(b.chunks_exact(8)
        .map(|v| u64::from_le_bytes(v.try_into().unwrap()))
        .collect())
}
fn words32(m: &Memory, p: u64, n: usize) -> Result<Vec<u32>> {
    let mut b = vec![0; n * 4];
    m.read(p, &mut b)?;
    Ok(b.chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect())
}
fn write_words(m: &mut Memory, p: u64, b: &[u64], n: usize) -> Result<()> {
    let bytes: Vec<_> = b.iter().flat_map(|x| x.to_le_bytes()).collect();
    write(m, p, &bytes[..n])
}

#[cfg(test)]
mod tests {
    use super::*;
    use trames_memory::{Permissions, PAGE_SIZE};
    #[test]
    fn native_transfers_cross_pages_keep_cow_and_preflight_permissions() {
        let mut memory = Memory::new(3 * PAGE_SIZE as usize);
        memory.map(0x1000, 3 * PAGE_SIZE, Permissions::RW).unwrap();
        let mut source: Vec<u8> = (0..5000).map(|i| i as u8).collect();
        memory.write(0x1ff3, &source).unwrap();
        let parent = memory.clone();
        let mut native = vec![0; source.len()];
        let upload =
            unsafe { transfer_pages(&mut memory, 0x1ff3, native.as_mut_ptr(), native.len(), true) }
                .unwrap();
        assert_eq!(native, source);
        assert_eq!(upload.direct_bytes, source.len() as u64);
        source.fill(42);
        native.copy_from_slice(&source);
        let download = unsafe {
            transfer_pages(
                &mut memory,
                0x1ff3,
                native.as_mut_ptr(),
                native.len(),
                false,
            )
        }
        .unwrap();
        let mut current = vec![0; source.len()];
        memory.read(0x1ff3, &mut current).unwrap();
        assert_eq!(current, source);
        parent.read(0x1ff3, &mut current).unwrap();
        assert_ne!(current, source);
        assert_eq!(download.direct_bytes, source.len() as u64);
        memory.protect(0x3000, PAGE_SIZE, Permissions::RX).unwrap();
        native.fill(7);
        assert!(unsafe {
            transfer_pages(
                &mut memory,
                0x1ff3,
                native.as_mut_ptr(),
                native.len(),
                false,
            )
        }
        .is_err());
        memory.read(0x1ff3, &mut current).unwrap();
        assert_eq!(current, source);
    }
    #[test]
    fn specialization_records_keep_offsets_and_reject_overflow_or_duplicate_ids() {
        let entries = specialization_entries(&[1, 4, 4, 0, 8, 4], 16).unwrap();
        assert_eq!(
            (entries[0].constant_id, entries[0].offset, entries[0].size),
            (1, 4, 4)
        );
        assert_eq!((entries[1].constant_id, entries[1].offset), (0, 8));
        for invalid in [
            vec![0, 12, 8],
            vec![0, 0, 0],
            vec![0, 1, u64::MAX],
            vec![u64::MAX, 0, 4],
            vec![0, 0, 4, 0, 4, 4],
        ] {
            assert!(specialization_entries(&invalid, 16).is_err());
        }
    }
    #[test]
    fn headless_wait_stages_do_not_enable_graphics_or_unknown_extension_bits() {
        assert!(compute_stage(vk::PipelineStageFlags::COMPUTE_SHADER.as_raw()).is_ok());
        assert!(compute_stage(vk::PipelineStageFlags::ALL_COMMANDS.as_raw()).is_ok());
        for stage in [
            0,
            vk::PipelineStageFlags::FRAGMENT_SHADER.as_raw(),
            u32::MAX,
        ] {
            assert!(compute_stage(stage).is_err());
        }
    }
    #[test]
    fn malformed_transport_cannot_read_guest_memory_or_create_driver_objects() {
        let mut b = Backend::new();
        let mut memory = Memory::new(PAGE_SIZE as usize);
        assert_eq!(
            b.invoke(&mut memory, 1, 0, 127).unwrap(),
            Invocation::Complete(vk::Result::ERROR_UNKNOWN.as_raw() as i64 as u64)
        );
        assert!(b.instance.is_none());
        assert!(b.objects.is_empty());
        assert!(b.invoke(&mut memory, 1, 0, 128).is_err());
        assert!(b.instance.is_none());
        assert!(b.objects.is_empty());
    }
    #[test]
    fn unknown_object_domains_and_uninitialized_driver_never_become_host_handles() {
        let mut left = Backend::new();
        let right = Backend::new();
        assert_ne!(left.domain, right.domain);
        assert!(left.object((right.domain << 32) | 1, Kind::Memory).is_err());
        let mut memory = Memory::new(PAGE_SIZE as usize);
        memory.map(0x1000, PAGE_SIZE, Permissions::RW).unwrap();
        let result = left.invoke(&mut memory, 13, 0x1000, 128).unwrap();
        assert_eq!(
            result,
            Invocation::Complete(vk::Result::ERROR_INITIALIZATION_FAILED.as_raw() as i64 as u64)
        );
        assert!(left.objects.is_empty());
    }
}

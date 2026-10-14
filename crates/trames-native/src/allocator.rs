// SPDX-License-Identifier: GPL-3.0-or-later
//! Original mallocng hot algorithms, ahead-of-time lowered to checked accesses.
use core::ffi::c_void;

/// Every callback completes before C checks its result and propagates failure.
/// No guest address is dereferenced in C and no callback pointer escapes.
pub trait Access {
    fn load(&mut self, address: u64, width: usize) -> Result<u64, AccessFailure>;
    fn store(&mut self, address: u64, width: usize, value: u64) -> Result<(), AccessFailure>;
    fn cas(&mut self, address: u64, old: u32, new: u32) -> Result<u32, AccessFailure>;
}
#[derive(Debug)]
pub struct AccessFailure;
#[repr(C)]
struct Callbacks {
    opaque: *mut c_void,
    load: unsafe extern "C" fn(*mut c_void, u64, u32, *mut u64) -> i32,
    store: unsafe extern "C" fn(*mut c_void, u64, u32, u64) -> i32,
    cas: unsafe extern "C" fn(*mut c_void, u64, u32, u32, *mut u32) -> i32,
}
#[repr(C)]
#[derive(Default, Debug)]
pub struct ResultState {
    pub value: u64,
    pub meta: u64,
    pub index: u64,
    pub counter: u64,
    pub status: i32,
}
unsafe extern "C" {
    fn trames_native_allocator(
        callbacks: *const Callbacks,
        op: u32,
        pointer_or_size: u64,
        context: u64,
        mt: i32,
        index: u64,
        counter: u64,
        result: *mut ResultState,
    );
}
unsafe extern "C" fn load<A: Access>(p: *mut c_void, a: u64, n: u32, out: *mut u64) -> i32 {
    // SAFETY: execute lends exactly one exclusive A for the synchronous call;
    // C supplies a live stack output and retains neither pointer.
    match unsafe { (&mut *p.cast::<A>()).load(a, n as usize) } {
        Ok(value) => {
            unsafe { *out = value };
            0
        }
        Err(AccessFailure) => 1,
    }
}
unsafe extern "C" fn store<A: Access>(p: *mut c_void, a: u64, n: u32, value: u64) -> i32 {
    // SAFETY: Same scoped exclusive callback context as load.
    i32::from(unsafe { (&mut *p.cast::<A>()).store(a, n as usize, value) }.is_err())
}
unsafe extern "C" fn cas<A: Access>(
    p: *mut c_void,
    a: u64,
    old: u32,
    new: u32,
    out: *mut u32,
) -> i32 {
    // SAFETY: Same context; the C uint32_t output is live for this callback.
    match unsafe { (&mut *p.cast::<A>()).cas(a, old, new) } {
        Ok(value) => {
            unsafe { *out = value };
            0
        }
        Err(AccessFailure) => 1,
    }
}
pub fn execute<A: Access>(
    access: &mut A,
    op: u32,
    value: u64,
    context: u64,
    mt: bool,
    index: u64,
    counter: u64,
) -> ResultState {
    let cb = Callbacks {
        opaque: (access as *mut A).cast(),
        load: load::<A>,
        store: store::<A>,
        cas: cas::<A>,
    };
    let mut result = ResultState::default();
    // SAFETY: Private matched C ABI; only checked scalar callbacks access guest
    // memory. Native stack and callback borrows remain within this invocation.
    unsafe {
        trames_native_allocator(
            &cb,
            op,
            value,
            context,
            i32::from(mt),
            index,
            counter,
            &mut result,
        )
    };
    result
}

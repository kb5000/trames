// SPDX-License-Identifier: GPL-3.0-or-later
//! Version 2 embedding ABI. All v1 functions retain their signatures/semantics.
//! All runtime/VFS objects stay on their owning worker. Only bounded byte copies
//! require unsafe code; callers own and keep their C buffers valid for each call.
mod session;
use session::{Options, Session, TransferOptions, QUEUE_LIMIT};
use std::{
    cell::RefCell,
    collections::BTreeMap,
    panic::{catch_unwind, AssertUnwindSafe},
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc, Mutex, OnceLock,
    },
};
static REGISTRY: OnceLock<Mutex<BTreeMap<u64, Arc<Session>>>> = OnceLock::new();
static NEXT_HANDLE: AtomicU64 = AtomicU64::new(1);
thread_local! { static LAST_ERROR: RefCell<String> = const { RefCell::new(String::new()) }; }
fn error(message: impl ToString) -> i32 {
    LAST_ERROR.with(|e| *e.borrow_mut() = message.to_string().chars().take(4096).collect());
    -1
}
fn boundary(f: impl FnOnce() -> i32) -> i32 {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or_else(|_| error("embedding ABI panicked"))
}
fn registry() -> &'static Mutex<BTreeMap<u64, Arc<Session>>> {
    REGISTRY.get_or_init(Mutex::default)
}
fn session(handle: u64) -> Result<Arc<Session>, &'static str> {
    registry()
        .lock()
        .map_err(|_| "handle registry unavailable")?
        .get(&handle)
        .cloned()
        .ok_or("invalid or closed session handle")
}
/// # Safety
/// A nonnull pointer must reference `length` readable bytes for the call. Null is
/// allowed only for a zero-length buffer. Buffer length is capped at 64 KiB.
unsafe fn bytes<'a>(pointer: *const u8, length: u32) -> Result<&'a [u8], &'static str> {
    if length as usize > QUEUE_LIMIT || (pointer.is_null() && length != 0) {
        return Err("invalid byte buffer");
    }
    if length == 0 {
        return Ok(&[]);
    }
    // SAFETY: the embedding caller guarantees a live readable buffer; length
    // and null checks above constrain the copied range.
    Ok(unsafe { std::slice::from_raw_parts(pointer, length as usize) })
}
/// # Safety
/// The caller must provide `capacity` writable bytes. Null is allowed only if
/// capacity is zero. No pointer is retained after this function returns.
unsafe fn copy(pointer: *mut u8, capacity: u32, source: &[u8]) -> Result<i32, &'static str> {
    if capacity as usize > QUEUE_LIMIT
        || source.len() > capacity as usize
        || (pointer.is_null() && capacity != 0)
    {
        return Err("output buffer too small or invalid");
    }
    if !source.is_empty() {
        // SAFETY: source is Rust-owned, destination is caller-owned and disjoint.
        unsafe {
            std::ptr::copy_nonoverlapping(source.as_ptr(), pointer, source.len());
        }
    }
    Ok(source.len() as i32)
}
#[no_mangle]
pub extern "C" fn trames_abi_version() -> u32 {
    2
}
#[no_mangle]
pub extern "C" fn trames_execution_backend_available(backend: u32) -> u32 {
    u32::from(backend == 0 || (backend == 1 && trames_core::cpp_available()))
}
/// # Safety
/// See `bytes`. Native-authorized store path only. Serialize against new opens.
/// Returns 1 while a cancelled/live worker holds the store, 0 after deletion.
#[no_mangle]
pub unsafe extern "C" fn trames_environment_reset(pointer: *const u8, length: u32) -> i32 {
    #[derive(serde::Deserialize)]
    #[serde(deny_unknown_fields)]
    struct Reset {
        store: std::path::PathBuf,
    }
    boundary(|| {
        let result = (|| {
            let options: Reset =
                serde_json::from_slice(unsafe { bytes(pointer, length) }.map_err(str::to_string)?)
                    .map_err(|e| e.to_string())?;
            if !options.store.is_absolute() || options.store.as_os_str().len() > 4096 {
                return Err("invalid store path".into());
            }
            match trames_vfs::Vfs::reset_disk(options.store) {
                Ok(()) => Ok(0),
                Err(trames_vfs::Errno(16)) => Ok(1),
                Err(e) => Err(e.to_string()),
            }
        })();
        result.unwrap_or_else(error)
    })
}
/// # Safety
/// See `copy`. Returns one JSON request, 0 if none, -1 on error. A small buffer
/// never consumes a request. Call from the native consent broker, not the guest.
#[no_mangle]
pub unsafe extern "C" fn trames_session_bridge_poll(
    handle: u64,
    pointer: *mut u8,
    capacity: u32,
) -> i32 {
    boundary(|| {
        let result = (|| {
            // Validate the destination before claiming a request.
            unsafe { copy(pointer, capacity, &[]) }.map_err(str::to_string)?;
            let data = session(handle)
                .map_err(str::to_string)?
                .bridge
                .poll(capacity as usize)
                .map_err(|e| format!("Bridge errno {e}"))?;
            match data {
                Some(data) => unsafe { copy(pointer, capacity, &data) }.map_err(str::to_string),
                None => Ok(0),
            }
        })();
        result.unwrap_or_else(error)
    })
}
/// # Safety
/// See `bytes`. Completing a stale, cancelled or expired ticket fails. Exactly
/// one of result/error is required. Replies contain no retained native pointers.
#[no_mangle]
pub unsafe extern "C" fn trames_session_bridge_reply(
    handle: u64,
    pointer: *const u8,
    length: u32,
) -> i32 {
    boundary(|| {
        let result = (|| {
            session(handle)
                .map_err(str::to_string)?
                .bridge
                .reply(unsafe { bytes(pointer, length) }.map_err(str::to_string)?)
                .map_err(|e| format!("Bridge errno {e}"))
        })();
        result.map(|_| 0).unwrap_or_else(error)
    })
}
#[no_mangle]
pub extern "C" fn trames_session_bridge_active(handle: u64, ticket: u64) -> i32 {
    boundary(|| {
        session(handle)
            .map(|s| i32::from(s.bridge.active(ticket)))
            .unwrap_or_else(error)
    })
}
/// # Safety
/// See `bytes`. Blocking installation into an absent staging store; the native
/// adapter promotes it only after success. No worker retains the store lock.
#[no_mangle]
pub unsafe extern "C" fn trames_environment_install(pointer: *const u8, length: u32) -> i32 {
    boundary(|| {
        let result = (|| {
            let options: Options =
                serde_json::from_slice(unsafe { bytes(pointer, length) }.map_err(str::to_string)?)
                    .map_err(|e| e.to_string())?;
            session::install(options)
        })();
        result.map(|_| 0).unwrap_or_else(error)
    })
}
/// # Safety
/// See `bytes`. The JSON configuration is copied, never retained by pointer.
#[no_mangle]
pub unsafe extern "C" fn trames_session_open(pointer: *const u8, length: u32) -> u64 {
    let result = catch_unwind(AssertUnwindSafe(|| -> Result<u64, String> {
        let config: Options =
            serde_json::from_slice(unsafe { bytes(pointer, length) }.map_err(str::to_string)?)
                .map_err(|e| e.to_string())?;
        let mut registry = registry()
            .lock()
            .map_err(|_| "handle registry unavailable")?;
        let handle = NEXT_HANDLE
            .fetch_update(Ordering::AcqRel, Ordering::Acquire, |n| n.checked_add(1))
            .map_err(|_| "session handles exhausted")?;
        let session = Session::start(config)?;
        registry.insert(handle, session);
        Ok(handle)
    }));
    match result {
        Ok(Ok(handle)) => handle,
        Ok(Err(e)) => {
            error(e);
            0
        }
        Err(_) => {
            error("embedding ABI panicked");
            0
        }
    }
}
/// # Safety
/// See `bytes`. Returns accepted bytes; retry the remaining bytes on backpressure.
#[no_mangle]
pub unsafe extern "C" fn trames_session_input(handle: u64, pointer: *const u8, length: u32) -> i32 {
    boundary(|| {
        let result = (|| {
            let data = unsafe { bytes(pointer, length) }?;
            let session = session(handle)?;
            let mut b = session
                .buffers
                .lock()
                .map_err(|_| "session lock unavailable")?;
            let count = data.len().min(QUEUE_LIMIT - b.input.len());
            b.input.extend(&data[..count]);
            Ok::<_, &str>(count as i32)
        })();
        result.unwrap_or_else(error)
    })
}
/// # Safety
/// See `copy`. Output is drained only after a valid destination was supplied.
#[no_mangle]
pub unsafe extern "C" fn trames_session_output(
    handle: u64,
    pointer: *mut u8,
    capacity: u32,
) -> i32 {
    boundary(|| {
        let result = (|| {
            let session = session(handle)?;
            let mut b = session
                .buffers
                .lock()
                .map_err(|_| "session lock unavailable")?;
            let count = b.output.len().min(capacity as usize);
            let data: Vec<_> = b.output.iter().take(count).copied().collect();
            let count = unsafe { copy(pointer, capacity, &data) }?;
            b.output.drain(..count as usize);
            Ok::<_, &str>(count)
        })();
        result.unwrap_or_else(error)
    })
}
#[no_mangle]
pub extern "C" fn trames_session_resize(handle: u64, rows: u16, columns: u16) -> i32 {
    boundary(|| {
        if rows == 0 || columns == 0 || rows > 4096 || columns > 4096 {
            return error("invalid terminal size");
        }
        match session(handle) {
            Ok(s) => match s.buffers.lock() {
                Ok(mut b) => {
                    b.size = Some((rows, columns));
                    0
                }
                Err(_) => error("session lock unavailable"),
            },
            Err(e) => error(e),
        }
    })
}
#[no_mangle]
pub extern "C" fn trames_session_pause(handle: u64, paused: u32) -> i32 {
    boundary(|| match session(handle) {
        Ok(s) => {
            s.paused.store(paused != 0, Ordering::Release);
            0
        }
        Err(e) => error(e),
    })
}
#[no_mangle]
pub extern "C" fn trames_session_transfer_begin(handle: u64) -> u64 {
    match catch_unwind(AssertUnwindSafe(|| {
        session(handle).map_err(str::to_string)?.begin_transfer()
    })) {
        Ok(Ok(token)) => token,
        Ok(Err(e)) => {
            error(e);
            0
        }
        Err(_) => {
            error("embedding ABI panicked");
            0
        }
    }
}
#[no_mangle]
pub extern "C" fn trames_session_transfer_cancel(handle: u64, token: u64) -> i32 {
    boundary(|| {
        session(handle)
            .map_err(str::to_string)
            .and_then(|s| s.cancel_transfer(token))
            .map(|_| 0)
            .unwrap_or_else(error)
    })
}
/// # Safety
/// See `copy`. Status is UTF-8 JSON, without a trailing NUL. Does not drain output.
#[no_mangle]
pub unsafe extern "C" fn trames_session_status(
    handle: u64,
    pointer: *mut u8,
    capacity: u32,
) -> i32 {
    boundary(|| {
        let result = (|| {
            let session = session(handle).map_err(str::to_string)?;
            let b = session
                .buffers
                .lock()
                .map_err(|_| "session lock unavailable")?;
            let data = serde_json::to_vec(&b.status).map_err(|e| e.to_string())?;
            unsafe { copy(pointer, capacity, &data) }.map_err(str::to_string)
        })();
        result.unwrap_or_else(error)
    })
}
/// # Safety
/// See `bytes`. Blocking: call on a native transfer queue, never on the UI thread.
/// The host path must be authorized by the embedding app (Files security scope).
#[no_mangle]
pub unsafe extern "C" fn trames_session_transfer(
    handle: u64,
    pointer: *const u8,
    length: u32,
) -> i32 {
    boundary(|| {
        let result = (|| {
            let config: TransferOptions =
                serde_json::from_slice(unsafe { bytes(pointer, length) }.map_err(str::to_string)?)
                    .map_err(|e| e.to_string())?;
            session(handle).map_err(str::to_string)?.transfer(config)
        })();
        result.map(|_| 0).unwrap_or_else(error)
    })
}
#[no_mangle]
pub extern "C" fn trames_session_close(handle: u64) -> i32 {
    boundary(|| match registry().lock() {
        Ok(mut registry) => match registry.remove(&handle) {
            Some(s) => {
                s.cancelled.store(true, Ordering::Release);
                0
            }
            None => error("invalid or closed session handle"),
        },
        Err(_) => error("handle registry unavailable"),
    })
}
/// # Safety
/// See `copy`. Thread-local error text, without a trailing NUL. Read on the same
/// native thread as the failed ABI call. This function cannot consume the error.
#[no_mangle]
pub unsafe extern "C" fn trames_last_error(pointer: *mut u8, capacity: u32) -> i32 {
    boundary(|| {
        LAST_ERROR.with(|e| unsafe { copy(pointer, capacity, e.borrow().as_bytes()) }.unwrap_or(-1))
    })
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn abi_rejects_invalid_buffers_config_and_closed_handles() {
        assert_eq!(trames_abi_version(), 2);
        unsafe {
            assert_eq!(trames_session_open(std::ptr::null(), 1), 0);
            assert_eq!(trames_session_input(u64::MAX, std::ptr::null(), 0), -1);
            assert_eq!(trames_session_output(u64::MAX, std::ptr::null_mut(), 1), -1);
            assert_eq!(trames_session_open(b"{}".as_ptr(), 2), 0);
            let mut error = [0; 8192];
            assert!(trames_last_error(error.as_mut_ptr(), error.len() as u32) > 0);
        }
        assert_eq!(trames_session_close(u64::MAX), -1);
    }
}

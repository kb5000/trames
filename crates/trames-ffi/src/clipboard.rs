// SPDX-License-Identifier: GPL-3.0-or-later
//! Bounded off-thread text transfers, using the shared native clipboard provider.
//! UIKit performs effects; the compositor never blocks.
use crate::session::Session;
use serde_json::{json, Value};
use std::{
    io::{self, Read, Write},
    os::fd::OwnedFd,
    sync::{
        atomic::{AtomicU64, AtomicUsize, Ordering},
        Arc, Weak,
    },
    thread,
    time::{Duration, Instant},
};
use trames_shared::NativePipe;
use trames_wayland::HostClipboard;
const LIMIT: usize = 16384;
pub(crate) struct Clipboard {
    session: Weak<Session>,
    active: Arc<AtomicUsize>,
    generation: Arc<AtomicU64>,
}
struct Permit(Arc<AtomicUsize>);
impl Drop for Permit {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::Release);
    }
}
impl Clipboard {
    pub fn new(session: &Arc<Session>) -> Self {
        Self {
            session: Arc::downgrade(session),
            active: Arc::new(AtomicUsize::new(0)),
            generation: Arc::new(AtomicU64::new(0)),
        }
    }
    fn launch(&self, pipe: NativePipe, writing: bool) -> io::Result<()> {
        self.active
            .try_update(Ordering::AcqRel, Ordering::Acquire, |n| {
                (n < 4).then_some(n + 1)
            })
            .map_err(|_| io::Error::from(io::ErrorKind::WouldBlock))?;
        let permit = Permit(self.active.clone());
        let weak = self.session.clone();
        let generation = self.generation.clone();
        let version = if writing {
            generation.fetch_add(1, Ordering::AcqRel) + 1
        } else {
            0
        };
        thread::Builder::new()
            .name("trames-clipboard".into())
            .spawn(move || {
                let _permit = permit;
                let deadline = Instant::now() + Duration::from_secs(120);
                let current = || {
                    Instant::now() < deadline
                        && (!writing || generation.load(Ordering::Acquire) == version)
                        && weak
                            .upgrade()
                            .is_some_and(|s| !s.cancelled.load(Ordering::Acquire))
                };
                let Some(broker) = weak.upgrade().map(|s| s.bridge.clone()) else {
                    return;
                };
                transfer(pipe, writing, broker, current);
            })?;
        Ok(())
    }
}
fn transfer(
    mut pipe: NativePipe,
    writing: bool,
    broker: trames_bridge::Broker,
    current: impl Fn() -> bool,
) {
    let Ok(connection) = broker.connect() else {
        return;
    };
    let mut text = Vec::new();
    if writing {
        let mut bytes = [0; 4096];
        while current() {
            match pipe.read(&mut bytes) {
                Ok(0) => break,
                Ok(n) if text.len() + n <= LIMIT => text.extend_from_slice(&bytes[..n]),
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                    thread::sleep(Duration::from_millis(10))
                }
                _ => return,
            }
        }
    }
    if !current() {
        return;
    }
    let params = if writing {
        let Ok(text) = String::from_utf8(text) else {
            return;
        };
        json!({"text":text})
    } else {
        json!({})
    };
    let mut frame = serde_json::to_vec(&json!({"version":1,"id":1,
                "method":if writing {"clipboard.write"} else {"clipboard.read"},"params":params}))
    .unwrap();
    frame.push(b'\n');
    if connection.write(&frame) != Ok(frame.len()) {
        return;
    }
    let mut reply = Vec::new();
    while current() {
        match connection.read(32768) {
            Ok(bytes) => {
                reply.extend(bytes);
                if reply.len() > 32768 {
                    return;
                }
                if reply.contains(&b'\n') {
                    break;
                }
            }
            Err(11) => thread::sleep(Duration::from_millis(10)),
            _ => return,
        }
    }
    if !current() || writing {
        return;
    }
    let Ok(reply) = serde_json::from_slice::<Value>(&reply) else {
        return;
    };
    let Some(text) = reply
        .get("result")
        .and_then(|r| r.get("text"))
        .and_then(Value::as_str)
        .filter(|s| s.len() <= LIMIT)
    else {
        return;
    };
    let mut bytes = text.as_bytes();
    while !bytes.is_empty() && current() {
        match pipe.write(bytes) {
            Ok(0) => return,
            Ok(n) => bytes = &bytes[n..],
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                thread::sleep(Duration::from_millis(10))
            }
            _ => return,
        }
    }
}
impl HostClipboard for Clipboard {
    fn changed(&self) {
        self.generation.fetch_add(1, Ordering::AcqRel);
    }
    fn read(&self, fd: OwnedFd) -> io::Result<()> {
        self.launch(NativePipe::import(fd)?, false)
    }
    fn write(&self, pipe: NativePipe) -> io::Result<()> {
        self.launch(pipe, true)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn compositor_text_read_write_wait_for_host_and_preserve_unicode() {
        for writing in [false, true] {
            let broker = trames_bridge::Broker::default();
            let native = broker.clone();
            let (mut read, mut write) = NativePipe::pair().unwrap();
            let expected = "clipboard 中文 🧪";
            if writing {
                write.write_all(expected.as_bytes()).unwrap();
                drop(write);
                let worker = thread::spawn(move || transfer(read, true, native, || true));
                let request = wait(&broker);
                assert_eq!(request["request"]["params"]["text"], expected);
                broker.reply(&serde_json::to_vec(&json!({"version":1,"ticket":request["ticket"],"result":{"written":true}})).unwrap()).unwrap();
                worker.join().unwrap();
            } else {
                let worker = thread::spawn(move || transfer(write, false, native, || true));
                let request = wait(&broker);
                let mut bytes = [0; 128];
                assert_eq!(
                    read.read(&mut bytes).unwrap_err().kind(),
                    io::ErrorKind::WouldBlock
                );
                broker.reply(&serde_json::to_vec(&json!({"version":1,"ticket":request["ticket"],"result":{"text":expected}})).unwrap()).unwrap();
                worker.join().unwrap();
                let n = read.read(&mut bytes).unwrap();
                assert_eq!(&bytes[..n], expected.as_bytes());
                assert_eq!(read.read(&mut bytes).unwrap(), 0);
            }
        }
    }
    fn wait(broker: &trames_bridge::Broker) -> Value {
        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            if let Some(value) = broker.poll(32768).unwrap() {
                return serde_json::from_slice(&value).unwrap();
            }
            assert!(Instant::now() < deadline);
            thread::sleep(Duration::from_millis(1));
        }
    }
}

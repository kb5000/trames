// SPDX-License-Identifier: GPL-3.0-or-later
//! Owned native work. No guest pointer, memory guard or coordinator Rc crosses.
use std::{sync::mpsc, thread};

pub(super) struct Task<T> {
    result: mpsc::Receiver<T>,
    worker: Option<thread::JoinHandle<()>>,
}
impl<T: Send + 'static> Task<T> {
    pub fn spawn(work: impl FnOnce() -> T + Send + 'static) -> std::io::Result<Self> {
        let (sender, result) = mpsc::channel();
        let worker = thread::Builder::new()
            .name("trames-gpu-compile".into())
            .spawn(move || {
                // If the owner has gone away, dropping the result frees its native
                // object on this worker while the owned driver remains alive.
                let _ = sender.send(work());
            })?;
        Ok(Self {
            result,
            worker: Some(worker),
        })
    }
    pub fn poll(&mut self) -> Result<Option<T>, ()> {
        match self.result.try_recv() {
            Ok(value) => Ok(Some(value)),
            Err(mpsc::TryRecvError::Empty) => Ok(None),
            Err(mpsc::TryRecvError::Disconnected) => Err(()),
        }
    }
}
impl<T> Drop for Task<T> {
    fn drop(&mut self) {
        // Final context teardown joins before coordinator-held dependencies
        // disappear. Ordinary polling never joins unfinished native work.
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn slow_tasks_do_not_block_the_caller_or_an_unrelated_task() {
        let (release, gate) = mpsc::channel();
        let mut slow = Task::spawn(move || gate.recv().unwrap()).unwrap();
        assert_eq!(slow.poll(), Ok(None));
        let (done, completed) = mpsc::channel();
        let mut fast = Task::spawn(move || {
            done.send(()).unwrap();
            7
        })
        .unwrap();
        completed
            .recv_timeout(std::time::Duration::from_secs(2))
            .unwrap();
        // Receipt of the test marker precedes delivery of the task result.
        release.send(3).unwrap();
        for task in [&mut slow, &mut fast] {
            while task.poll().unwrap().is_none() {
                thread::yield_now();
            }
        }
    }
    #[test]
    fn teardown_joins_before_owned_dependency_destruction() {
        let finished = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let peer = finished.clone();
        let task =
            Task::spawn(move || peer.store(true, std::sync::atomic::Ordering::Release)).unwrap();
        drop(task);
        assert!(finished.load(std::sync::atomic::Ordering::Acquire));
    }
}

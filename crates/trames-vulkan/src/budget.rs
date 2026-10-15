// SPDX-License-Identifier: GPL-3.0-or-later
//! GPU allocations share a session budget even across guest address spaces.
use std::sync::{
    atomic::{AtomicU64, Ordering},
    Arc,
};
#[derive(Clone)]
pub struct Budget {
    used: Arc<AtomicU64>,
    compiling: Arc<AtomicU64>,
    limit: u64,
}
impl Default for Budget {
    fn default() -> Self {
        Self {
            used: Arc::new(AtomicU64::new(0)),
            compiling: Arc::new(AtomicU64::new(0)),
            limit: 128 << 20,
        }
    }
}
impl Budget {
    pub(crate) fn compile(&self) -> Option<CompilePermit> {
        self.compiling
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |n| {
                (n < 8).then_some(n + 1)
            })
            .ok()?;
        Some(CompilePermit(self.compiling.clone()))
    }
    pub(crate) fn reserve(&self, size: u64) -> bool {
        self.used
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |used| {
                used.checked_add(size).filter(|&n| n <= self.limit)
            })
            .is_ok()
    }
    pub(crate) fn release(&self, size: u64) {
        self.used.fetch_sub(size, Ordering::Relaxed);
    }
    pub fn used(&self) -> u64 {
        self.used.load(Ordering::Relaxed)
    }
}
pub(crate) struct CompilePermit(Arc<AtomicU64>);
impl Drop for CompilePermit {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::Relaxed);
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn compiler_capacity_is_shared_and_released_without_a_global_work_lock() {
        let budget = Budget::default();
        let other = budget.clone();
        let permits: Vec<_> = (0..8).map(|_| budget.compile().unwrap()).collect();
        assert!(other.compile().is_none());
        drop(permits);
        assert!(other.compile().is_some());
    }
    #[test]
    fn independent_contexts_share_budget_without_overcommit_or_lifetime_leak() {
        let budget = Budget::default();
        let peer = budget.clone();
        assert!(budget.reserve(64 << 20));
        assert!(peer.reserve(64 << 20));
        assert!(!budget.reserve(1));
        assert_eq!(peer.used(), 128 << 20);
        budget.release(64 << 20);
        assert!(peer.reserve(1));
        peer.release((64 << 20) + 1);
        assert_eq!(budget.used(), 0);
    }
}

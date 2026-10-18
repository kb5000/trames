// SPDX-License-Identifier: GPL-3.0-or-later
//! Guest signal queues and signalfd. Only the kernel coordinator touches these
//! queues; CPU workers and native accelerators need no new locks or checks.
use super::*;
use std::collections::VecDeque;

#[derive(Default)]
pub(super) struct Pending(BTreeMap<u32, VecDeque<Info>>);
impl Pending {
    pub(super) fn enqueue(&mut self, sig: u32, info: Info) -> Result<(), Errno> {
        if sig < 32 && self.0.contains_key(&sig) {
            return Ok(()); // Standard signals coalesce, preserving the first info.
        }
        if sig >= 32 && self.0.values().map(VecDeque::len).sum::<usize>() >= 4096 {
            return Err(Errno(11));
        }
        self.0.entry(sig).or_default().push_back(info);
        Ok(())
    }
    pub(super) fn remove(&mut self, sig: &u32) -> Option<Info> {
        let q = self.0.get_mut(sig)?;
        let info = q.pop_front();
        if q.is_empty() {
            self.0.remove(sig);
        }
        info
    }
    pub(super) fn discard(&mut self, sig: u32) {
        self.0.remove(&sig);
    }
    pub(super) fn clear(&mut self) {
        self.0.clear();
    }
    pub(super) fn bits(&self) -> u64 {
        self.0.keys().fold(0, |b, s| b | (1 << (s - 1)))
    }
    #[cfg(test)]
    pub(super) fn contains_key(&self, sig: &u32) -> bool {
        self.0.contains_key(sig)
    }
    fn next(&self, mask: u64) -> Option<u32> {
        let mut bits = self.bits() & mask;
        // Linux prioritizes synchronous-class signal numbers even when their
        // source is kill(), before selecting the lowest remaining number.
        const SYNCHRONOUS: u64 = (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 10) | (1 << 30);
        if bits & SYNCHRONOUS != 0 {
            bits &= SYNCHRONOUS;
        }
        (bits != 0).then(|| bits.trailing_zeros() + 1)
    }
    pub(super) fn values(&self) -> impl Iterator<Item = &Info> {
        self.0.values().flatten()
    }
    pub(super) fn iter(&self) -> impl Iterator<Item = (&u32, &Info)> {
        self.0
            .iter()
            .map(|(sig, q)| (sig, q.front().expect("nonempty signal queue")))
    }
    pub(super) fn timer_mut(&mut self, sig: u32, id: i32) -> Option<&mut Info> {
        self.0
            .get_mut(&sig)?
            .iter_mut()
            .find(|i| i.timer.is_some_and(|t| t.0 == id))
    }
    #[cfg(test)]
    pub(super) fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}
#[cfg(test)]
impl std::ops::Index<&u32> for Pending {
    type Output = Info;
    fn index(&self, sig: &u32) -> &Info {
        self.0[sig].front().unwrap()
    }
}

impl Process {
    pub(super) fn pending_bits(&self) -> u64 {
        self.pending.bits() | self.shared_pending.borrow().bits()
    }
    // Linux dequeues a task's private queue before its shared process queue.
    pub(super) fn next_signal(&self, mask: u64) -> Option<(u32, bool)> {
        self.pending.next(mask).map(|sig| (sig, false)).or_else(|| {
            self.shared_pending
                .borrow()
                .next(mask)
                .map(|sig| (sig, true))
        })
    }
    pub(super) fn take_signal(&mut self, sig: u32, shared: bool) -> Info {
        let mut info = if shared {
            self.shared_pending.borrow_mut().remove(&sig)
        } else {
            self.pending.remove(&sig)
        }
        .expect("selected pending signal");
        if info.fault.is_none() {
            info.sender = self.linux.vfs.visible_pid(info.sender);
        }
        info
    }
    pub(super) fn signal_senders(&self) -> Vec<u32> {
        self.pending
            .values()
            .chain(self.shared_pending.borrow().values())
            .map(|i| i.sender)
            .collect()
    }
}

fn write_signalfd(sig: u32, info: Info) -> [u8; 128] {
    let mut out = [0; 128];
    out[..4].copy_from_slice(&sig.to_le_bytes());
    out[8..12].copy_from_slice(&info.code.to_le_bytes());
    if let Some((id, overrun, value)) = info.timer {
        out[24..28].copy_from_slice(&id.to_le_bytes());
        out[32..36].copy_from_slice(&overrun.to_le_bytes());
        out[44..48].copy_from_slice(&(value as u32).to_le_bytes());
        out[48..56].copy_from_slice(&value.to_le_bytes());
    } else if let Some(address) = info.fault {
        out[72..80].copy_from_slice(&address.to_le_bytes());
        if sig == 31 && info.code == 1 {
            out[4..8].copy_from_slice(&info.sender.to_le_bytes());
            out[84..88].copy_from_slice(&info.status.to_le_bytes());
            out[88..96].copy_from_slice(&address.to_le_bytes());
            out[96..100].copy_from_slice(&info.uid.to_le_bytes());
        }
    } else {
        out[12..16].copy_from_slice(&info.sender.to_le_bytes());
        out[16..20].copy_from_slice(&info.uid.to_le_bytes());
        out[40..44].copy_from_slice(&info.status.to_le_bytes());
    }
    out
}

impl Kernel {
    pub(super) fn signalfd(&self, p: &mut Process, a: [u64; 6]) -> Result<Option<u64>, Errno> {
        if a[2] != 8 {
            return Err(Errno(22));
        }
        let mask = read_u64(&p.memory, a[1])?;
        p.linux
            .vfs
            .signalfd(a[0] as i32, mask, a[3] as u32)
            .map(|fd| Some(fd as u64))
    }
    pub(super) fn read_signalfd(
        &self,
        p: &mut Process,
        number: u32,
        a: [u64; 6],
    ) -> Result<Option<u64>, Errno> {
        let fd = a[0] as u32;
        let mask = p.linux.vfs.signalfd_mask(fd)?.ok_or(Errno(22))?;
        let (parts, count) = if number == 65 {
            if a[2] > 1024 {
                return Err(Errno(22));
            }
            Linux::check(&p.memory, a[1], a[2] as usize * 16, Access::Read)?;
            let mut parts = Vec::new();
            let mut requested = 0u64;
            let mut count = 0;
            for i in 0..a[2] {
                let address = read_u64(&p.memory, a[1] + i * 16)?;
                let length = read_u64(&p.memory, a[1] + i * 16 + 8)?;
                requested = requested
                    .checked_add(length)
                    .filter(|n| *n <= i64::MAX as u64)
                    .ok_or(Errno(22))?;
                let take = length.min((MAX_WRITE - count) as u64) as usize;
                parts.push((address, take));
                count += take;
            }
            if count == 0 {
                return Ok(Some(0));
            }
            (parts, count)
        } else {
            let count = a[2].min(MAX_WRITE as u64) as usize;
            (vec![(a[1], count)], count)
        };
        if count < 128 {
            return Err(Errno(22));
        }
        let mut total = 0;
        while total + 128 <= count {
            let Some((sig, shared)) = p.next_signal(mask) else {
                break;
            };
            // Linux dequeues before copying: a fault consumes this signal, and
            // complete preceding records still count as a successful short read.
            let record = write_signalfd(sig, p.take_signal(sig, shared));
            let mut offset = 0;
            for &(address, length) in &parts {
                let start = total.max(offset);
                let end = (total + 128).min(offset + length);
                if start < end {
                    let result = address
                        .checked_add((start - offset) as u64)
                        .ok_or(Errno(14))
                        .and_then(|address| {
                            p.memory
                                .write(address, &record[start - total..end - total])
                                .map_err(|_| Errno(14))
                        });
                    if let Err(e) = result {
                        return if total == 0 {
                            Err(e)
                        } else {
                            Ok(Some(total as u64))
                        };
                    }
                }
                offset += length;
            }
            total += 128;
        }
        if total == 0 {
            return if p.linux.vfs.flags(fd)? & O_NONBLOCK != 0 {
                Err(Errno(11))
            } else {
                Ok(None)
            };
        }
        Ok(Some(total as u64))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn realtime_budget_preserves_fifo_and_cannot_drop_sigkill() {
        let mut q = Pending::default();
        let info = |sender| Info {
            sender,
            uid: 123,
            status: 0,
            code: 0,
            timer: None,
            fault: None,
        };
        for i in 0..4096 {
            q.enqueue(34, info(i)).unwrap();
        }
        assert_eq!(q.enqueue(34, info(9999)), Err(Errno(11)));
        q.enqueue(9, info(1)).unwrap();
        q.enqueue(9, info(2)).unwrap();
        assert_eq!(q.remove(&9).unwrap().sender, 1);
        assert!(!q.contains_key(&9));
        for i in 0..4096 {
            assert_eq!(q.remove(&34).unwrap().sender, i);
        }
        assert!(q.is_empty());
    }
}

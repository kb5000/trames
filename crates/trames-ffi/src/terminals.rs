// SPDX-License-Identifier: GPL-3.0-or-later
//! Host PTY masters on the coordinator. Shells run unchanged guest BusyBox.
//! The init pipe is private: children inherit only their slave, never a master.
use crate::session::{Buffers, QUEUE_LIMIT};
use std::{
    collections::{BTreeMap, VecDeque},
    rc::Rc,
};
use trames_vfs::{Vfs, AT_FDCWD, O_CLOEXEC, O_NONBLOCK};

// Monitor mode prevents ash's asynchronous children inheriting ignored SIGINT.
// The private console enables normal job control. setsid then opens the slave
// in its own session. Retry interrupted reads when a launcher child exits.
pub(crate) const INIT: &str = "set -m; trap '' CHLD; while :; do if IFS= read -r tty <&3; then /bin/busybox setsid /bin/ash -c 'exec /bin/ash -i <\"$1\" >\"$1\" 2>&1' sh \"$tty\" 3<&- & fi; done";
pub(crate) const LIMIT: usize = 32;
pub(crate) struct Channel {
    pub input: VecDeque<u8>,
    pub output: VecDeque<u8>,
    pub size: Option<(u16, u16)>,
    pub phase: &'static str,
    pub error: Option<String>,
}
impl Default for Channel {
    fn default() -> Self {
        Self {
            input: VecDeque::new(),
            output: VecDeque::new(),
            size: None,
            phase: "starting",
            error: None,
        }
    }
}
struct Master {
    fd: u32,
    pair: Rc<trames_vfs::Pty>,
}
pub(crate) struct NativeTerminals {
    vfs: Vfs,
    control: u32,
    masters: BTreeMap<u64, Master>,
}
impl NativeTerminals {
    pub fn new(root: &mut Vfs) -> Result<Self, String> {
        let [reader, writer] = root.pipe(0).map_err(|e| e.to_string())?;
        if reader != 3 {
            return Err("init control descriptor 3 is unavailable".into());
        }
        let mut vfs = root.clone();
        root.close(writer).map_err(|e| e.to_string())?;
        vfs.close(reader).map_err(|e| e.to_string())?;
        Ok(Self {
            vfs,
            control: writer,
            masters: BTreeMap::new(),
        })
    }
    fn open(&mut self, rows: u16, columns: u16) -> Result<Master, String> {
        if self.masters.len() >= LIMIT {
            return Err("terminal limit reached".into());
        }
        let fd = self
            .vfs
            .open_at(AT_FDCWD, b"/dev/ptmx", 2 | O_NONBLOCK | O_CLOEXEC, 0)
            .map_err(|e| e.to_string())?;
        let result = (|| {
            let pair = self
                .vfs
                .pty(fd)
                .map_err(|e| e.to_string())?
                .ok_or("PTY master missing")?
                .pair
                .clone();
            pair.locked.set(false);
            pair.resize_from_host(rows, columns);
            let path = format!("/dev/pts/{}\n", pair.number);
            if self
                .vfs
                .write(self.control, path.as_bytes())
                .map_err(|e| e.to_string())?
                != path.len()
            {
                return Err("init control pipe full".into());
            }
            Ok(Master { fd, pair })
        })();
        if result.is_err() {
            let _ = self.vfs.close(fd);
        }
        result
    }
    pub fn pump(&mut self, buffers: &mut Buffers) {
        for (&id, channel) in &mut buffers.terminals {
            if !self.masters.contains_key(&id) && channel.phase == "starting" {
                let (rows, columns) = channel.size.take().unwrap_or((24, 80));
                match self.open(rows, columns) {
                    Ok(master) => {
                        self.masters.insert(id, master);
                    }
                    Err(error) => {
                        channel.phase = "failed";
                        channel.error = Some(error);
                    }
                }
            }
        }
        self.masters.retain(|id, master| {
            let Some(channel) = buffers.terminals.get_mut(id) else {
                // A close can race init's queued open. Retain the master until
                // that slave has opened, preventing its path/number being reused
                // for another request while the old command is still queued.
                if master
                    .pair
                    .slaves
                    .load(std::sync::atomic::Ordering::Relaxed)
                    == 0
                    && master.pair.poll(true, 0) & 16 == 0
                {
                    return true;
                }
                let _ = self.vfs.close(master.fd);
                return false;
            };
            if master
                .pair
                .slaves
                .load(std::sync::atomic::Ordering::Relaxed)
                != 0
                && channel.phase == "starting"
            {
                channel.phase = "running";
            }
            if let Some((rows, columns)) = channel.size.take() {
                master.pair.resize_from_host(rows, columns);
            }
            if !channel.input.is_empty() {
                let bytes: Vec<_> = channel.input.iter().take(4096).copied().collect();
                if let Ok((count, signals)) = master.pair.master_write(&bytes) {
                    channel.input.drain(..count);
                    master.pair.host_signals.set(
                        signals
                            .into_iter()
                            .fold(master.pair.host_signals.get(), |mask, sig| {
                                mask | (1 << (sig - 1))
                            }),
                    );
                }
            }
            master.pair.flush_echo();
            let room = QUEUE_LIMIT - channel.output.len();
            if room != 0 {
                match master.pair.master_read(room) {
                    Ok(bytes) => channel.output.extend(bytes),
                    Err(e) if e.0 == 5 => {
                        channel.phase = "exited";
                    }
                    Err(e) if e.0 == 11 => {}
                    Err(e) => {
                        channel.phase = "failed";
                        channel.error = Some(e.to_string());
                    }
                }
            }
            true
        });
    }
}

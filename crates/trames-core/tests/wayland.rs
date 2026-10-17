// SPDX-License-Identifier: GPL-3.0-or-later
//! Real, Alpine-signed, unmodified ARM64 Wayland clients inside the guest.
//! This headless sink checks protocol, pixels and text state, not Apple rendering.
use std::{
    io,
    time::{Duration, Instant},
};
use trames_core::{Runtime, RuntimeOptions};
use trames_linux::{GuestOutput, OutputFd};
use trames_wayland::{InputEvent, Server, TextEdit};
struct Sink {
    server: Server,
    began: Instant,
    stdout: Vec<u8>,
    stderr: Vec<u8>,
    frames: usize,
    last: u64,
    closed: bool,
    focused: bool,
    text_sent: bool,
    text_confirmed: bool,
    editor: bool,
    vulkan: bool,
    resized: bool,
    resize_confirmed: bool,
}
impl GuestOutput for Sink {
    fn write(&mut self, fd: OutputFd, bytes: &[u8]) -> io::Result<usize> {
        match fd {
            OutputFd::Stdout => &mut self.stdout,
            OutputFd::Stderr => &mut self.stderr,
        }
        .extend_from_slice(bytes);
        Ok(bytes.len())
    }
    fn control(&mut self, _: &mut trames_vfs::Vfs) -> io::Result<bool> {
        let snapshot = self.server.snapshot();
        for window in &snapshot.windows {
            if !self.focused {
                self.server.input(InputEvent::Focus {
                    window: Some(window.id),
                })?;
                if self.editor {
                    self.server.input(InputEvent::Pointer {
                        window: window.id,
                        x: 80.0,
                        y: 80.0,
                        time: 1,
                    })?;
                    self.server.input(InputEvent::Button {
                        button: 0x110,
                        pressed: true,
                        time: 2,
                    })?;
                    self.server.input(InputEvent::Button {
                        button: 0x110,
                        pressed: false,
                        time: 3,
                    })?;
                }
                self.focused = true;
            }
            if window.revision != self.last {
                self.last = window.revision;
                self.frames += 1;
                if self.vulkan {
                    if !self.resized && !window.scene.is_empty() {
                        self.server.resize(window.id, 320, 240)?;
                        self.resized = true;
                    }
                    self.resize_confirmed |= window.scene.iter().any(|i| {
                        matches!(i.buffer, trames_wayland::ImageBuffer::Gpu(_))
                            && i.buffer.dimensions() == (320, 240)
                    });
                }
                for image in &window.scene {
                    let Some(buffer) = image.buffer.shm() else {
                        continue;
                    };
                    let map = &buffer.mapping;
                    // SAFETY: this synchronous headless read retains the scene capability and
                    // accesses only validated buffer pixels while the client grants ownership.
                    unsafe {
                        map.with_read(0, 4, |pixel| {
                            assert_eq!(pixel.len(), 4);
                        })
                    };
                }
                self.server.presented(
                    window.id,
                    window.revision,
                    self.began.elapsed().as_millis() as u32,
                )?;
            }
            if (!self.editor && !self.vulkan && self.frames >= 8
                || self.editor && self.text_confirmed)
                && !self.closed
            {
                self.server.close(window.id)?;
                self.closed = true;
            }
        }
        if let Some(t) = snapshot.text {
            if !self.text_sent {
                self.server.edit_text(TextEdit {
                    serial: t.serial,
                    context: t.id,
                    epoch: t.epoch,
                    preedit: Some(("ni".into(), 2, 2)),
                    ..Default::default()
                })?;
                self.server.edit_text(TextEdit {
                    serial: t.serial,
                    context: t.id,
                    epoch: t.epoch,
                    preedit: Some((String::new(), 0, 0)),
                    commit: Some("你好🙂".into()),
                    ..Default::default()
                })?;
                self.text_sent = true;
            } else if t.text.contains("你好🙂") {
                self.text_confirmed = true;
            }
        }
        Ok(self.began.elapsed() < Duration::from_secs(90))
    }
}
fn run(program: &str, editor: bool) -> Sink {
    run_options(program, editor, false, &[])
}
fn run_options(program: &str, editor: bool, vulkan: bool, arguments: &[&str]) -> Sink {
    let path = std::env::var("TRAMES_WAYLAND_STORE")
        .expect("prepare original signed packages in a guest store");
    let mut vfs = trames_vfs::Vfs::open_disk(path, Default::default()).unwrap();
    let server = vfs.enable_wayland(b"/run/user/0").unwrap();
    if vulkan {
        for dir in [
            b"/usr/share/vulkan".as_slice(),
            b"/usr/share/vulkan/icd.d".as_slice(),
        ] {
            match vfs.mkdir_at(trames_vfs::AT_FDCWD, dir, 0o755) {
                Ok(()) | Err(trames_vfs::Errno(17)) => {}
                Err(e) => panic!("{e}"),
            }
        }
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        for (path, file) in [
            (
                "/usr/lib/libtrames_vulkan_icd.so",
                "target/vulkan-guest/libtrames_vulkan_icd.so",
            ),
            (
                "/usr/share/vulkan/icd.d/trames.json",
                "target/vulkan-guest/trames.json",
            ),
        ] {
            match vfs.unlink_at(trames_vfs::AT_FDCWD, path.as_bytes(), false) {
                Ok(()) | Err(trames_vfs::Errno(2)) => {}
                Err(e) => panic!("{e}"),
            }
            vfs.import_reader(
                path.as_bytes(),
                &mut std::fs::File::open(root.join(file)).unwrap(),
                false,
            )
            .unwrap();
        }
    }
    let bytes = vfs.executable(program.as_bytes()).unwrap();
    let mut argv = vec![program.to_owned()];
    argv.extend(arguments.iter().map(|s| (*s).to_owned()));
    let env = [
        "HOME=/root",
        "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json",
        "PATH=/usr/bin:/bin",
        "XDG_RUNTIME_DIR=/run/user/0",
        "WAYLAND_DISPLAY=wayland-0",
    ]
    .map(str::to_owned);
    let mut runtime = Runtime::load_with_options(
        &bytes,
        &argv,
        &env,
        vfs,
        RuntimeOptions {
            vulkan_accelerator: vulkan,
            ..Default::default()
        },
    )
    .unwrap();
    let mut sink = Sink {
        server,
        began: Instant::now(),
        stdout: vec![],
        stderr: vec![],
        frames: 0,
        last: 0,
        closed: false,
        focused: false,
        text_sent: false,
        text_confirmed: false,
        editor,
        vulkan,
        resized: false,
        resize_confirmed: false,
    };
    let result = runtime.run(&mut sink, 10_000_000_000);
    if !result.as_ref().is_ok_and(|e| e.code == 0) {
        eprintln!(
            "{}",
            String::from_utf8_lossy(&runtime.linux.vfs.syslog(3, 65536).unwrap_or_default())
        );
    }
    eprintln!("{program}: result={result:?} frames={} focused={} text_sent={} text_confirmed={} closed={}\nstdout={}\nstderr={}\nlog={}",sink.frames,sink.focused,sink.text_sent,sink.text_confirmed,sink.closed,String::from_utf8_lossy(&sink.stdout),String::from_utf8_lossy(&sink.stderr),String::from_utf8_lossy(&runtime.linux.vfs.syslog(3,65536).unwrap_or_default()));
    assert!(
        result.is_ok(),
        "{program}: {result:?}\nstdout={}\nstderr={}",
        String::from_utf8_lossy(&sink.stdout),
        String::from_utf8_lossy(&sink.stderr)
    );
    assert_eq!(
        result.unwrap().code,
        0,
        "{}",
        String::from_utf8_lossy(&sink.stderr)
    );
    sink
}
#[test]
#[ignore = "prepare signed wayland-utils and weston-clients; set TRAMES_WAYLAND_STORE"]
fn original_wayland_info_inside_guest() {
    let s = run("/usr/bin/wayland-info", false);
    let out = String::from_utf8_lossy(&s.stdout);
    assert!(
        out.contains("wl_seat") && out.contains("xdg_wm_base"),
        "{out}"
    );
}
#[test]
#[ignore = "prepare signed wayland-utils and weston-clients; set TRAMES_WAYLAND_STORE"]
fn original_weston_simple_shm_inside_guest() {
    let s = run("/usr/bin/weston-simple-shm", false);
    assert!(s.frames >= 8 && s.closed);
}
#[test]
#[ignore = "prepare signed wayland-utils and weston-clients; set TRAMES_WAYLAND_STORE"]
fn original_weston_editor_native_ime_inside_guest() {
    let s = run("/usr/bin/weston-editor", true);
    assert!(s.text_sent && s.text_confirmed && s.closed);
}

#[test]
#[ignore = "prepare original signed Vulkan tools/loader and current guest ICD; set TRAMES_WAYLAND_STORE"]
fn original_vkcube_wayland_inside_guest() {
    let s = run_options(
        "/usr/bin/vkcube",
        false,
        true,
        &["--wsi", "wayland", "--c", "12"],
    );
    assert!(s.frames > 0, "{}", String::from_utf8_lossy(&s.stderr));
    assert!(
        s.resized && s.resize_confirmed,
        "vkcube did not recreate its images after configure"
    );
}

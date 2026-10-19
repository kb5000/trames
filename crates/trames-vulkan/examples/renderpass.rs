// SPDX-License-Identifier: GPL-3.0-or-later
//! Original native driver regression for the new wire operations (also Metal).
use std::{
    cell::RefCell,
    rc::Rc,
    thread,
    time::{Duration, Instant},
};
use trames_memory::{Memory, MemoryError, PageBacking, Permissions, PAGE_SIZE};
use trames_vulkan::{Backend, Invocation};
const PACKET: u64 = 0x1000;
const OUT: u64 = 0x2000;
fn request(b: &mut Backend, m: &mut Memory, op: u64, args: &[u64]) -> u64 {
    let mut bytes = [0; 128];
    for (i, v) in args.iter().enumerate() {
        bytes[i * 8..i * 8 + 8].copy_from_slice(&v.to_le_bytes());
    }
    m.write(PACKET, &bytes).unwrap();
    match b.invoke(m, op, PACKET, 128).unwrap() {
        Invocation::Complete(value) => value,
        other => panic!("unexpected {other:?}"),
    }
}
fn call(b: &mut Backend, m: &mut Memory, op: u64, args: &[u64]) {
    assert_eq!(request(b, m, op, args), 0, "op {op}");
}
fn word(m: &Memory, at: u64) -> u64 {
    let mut bytes = [0; 8];
    m.read(at, &mut bytes).unwrap();
    u64::from_le_bytes(bytes)
}
fn words(m: &mut Memory, at: u64, values: &[u64]) {
    m.write(
        at,
        &values
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
}
fn poll(b: &mut Backend, m: &mut Memory, op: u64, args: &[u64], pending: u64) {
    let began = Instant::now();
    loop {
        let r = request(b, m, op, args);
        if r == 0 {
            break;
        }
        assert_eq!(r, pending);
        assert!(began.elapsed() < Duration::from_secs(10));
        thread::sleep(Duration::from_micros(100));
    }
}
struct FailedOutput(RefCell<[u8; PAGE_SIZE as usize]>);
impl PageBacking for FailedOutput {
    fn check(&self, _: u64) -> Result<(), MemoryError> {
        Ok(())
    }
    fn read(&self, at: usize, out: &mut [u8]) -> Result<(), MemoryError> {
        out.copy_from_slice(&self.0.borrow()[at..at + out.len()]);
        Ok(())
    }
    fn write(&self, _: usize, _: &[u8]) -> Result<(), MemoryError> {
        Err(MemoryError::BackingIo(5))
    }
    fn sync(&self) -> Result<(), MemoryError> {
        Ok(())
    }
    fn version(&self) -> u64 {
        0
    }
    fn writable(&self) -> bool {
        true
    }
    fn next_page(&self, _: u64) -> Result<Rc<dyn PageBacking>, MemoryError> {
        Err(MemoryError::InvalidRange)
    }
}
// Late-bound images are never framebuffer dependencies. Commands must retain
// them themselves, including the submission snapshot after a pool reset.
fn image(b: &mut Backend, m: &mut Memory, dev: u64, format: u64, usage: u64) -> (u64, u64, u64) {
    m.write(0x3500, &(format as u32).to_le_bytes()).unwrap();
    call(b, m, 60, &[dev, format, 16, 16, usage, OUT, 0, 0x3500, 1]);
    let image = word(m, OUT);
    call(b, m, 62, &[dev, image, 0x2100]);
    let size = word(m, 0x2100);
    let bits = word(m, 0x2110) as u32;
    call(b, m, 14, &[dev, size, bits.trailing_zeros() as u64, OUT]);
    let memory = word(m, OUT);
    call(b, m, 63, &[dev, image, memory, 0]);
    call(b, m, 64, &[dev, image, format, OUT]);
    (image, word(m, OUT), memory)
}
fn main() {
    let mut m = Memory::new(128 << 10);
    m.map(0x1000, 8 * PAGE_SIZE, Permissions::RW).unwrap();
    let mut b = Backend::new();
    call(&mut b, &mut m, 1, &[OUT]);
    let instance = word(&m, OUT);
    m.write(0x2080, &1u32.to_le_bytes()).unwrap();
    call(&mut b, &mut m, 3, &[instance, 0x2080, OUT]);
    let physical = word(&m, OUT);
    call(&mut b, &mut m, 115, &[physical, OUT]);
    assert_eq!(word(&m, OUT) & 248, 248);
    // All dependency extensions but no imageless feature: native guard must
    // reject the operation before touching invalid guest attachment handles.
    call(&mut b, &mut m, 8, &[physical, 0, OUT, 0, 0, 496]);
    let disabled = word(&m, OUT);
    assert_eq!(request(&mut b, &mut m, 125, &[disabled]), (-8i64) as u64);
    call(&mut b, &mut m, 9, &[disabled]);
    call(&mut b, &mut m, 8, &[physical, 0, OUT, 0, 0, 1008]);
    let dev = word(&m, OUT);
    call(&mut b, &mut m, 10, &[dev, 0, 0, OUT]);
    let queue = word(&m, OUT);
    call(&mut b, &mut m, 11, &[dev, 1024, 2, OUT]);
    let buffer = word(&m, OUT);
    call(&mut b, &mut m, 13, &[dev, buffer, 0x2100]);
    let size = word(&m, 0x2100);
    let bits = word(&m, 0x2110) as u32;
    call(&mut b, &mut m, 5, &[physical, 0x2500]);
    let mut bytes = [0; 4];
    m.read(0x2500, &mut bytes).unwrap();
    let count = u32::from_le_bytes(bytes);
    let memory_type = (0..count)
        .find(|&i| {
            m.read(0x2504 + i as u64 * 8, &mut bytes).unwrap();
            bits & (1 << i) != 0 && u32::from_le_bytes(bytes) & 6 == 6
        })
        .unwrap();
    call(&mut b, &mut m, 14, &[dev, size, memory_type as u64, OUT]);
    let memory = word(&m, OUT);
    call(&mut b, &mut m, 16, &[dev, buffer, memory, 0]);
    call(&mut b, &mut m, 17, &[dev, memory, 0, 1024, 0x4000]);
    call(&mut b, &mut m, 34, &[dev, 0, 2, OUT]);
    let pool = word(&m, OUT);
    call(&mut b, &mut m, 36, &[dev, pool, OUT]);
    let cmd = word(&m, OUT);
    call(&mut b, &mut m, 48, &[dev, 0, OUT]);
    let fence = word(&m, OUT);
    for depth in [false, true] {
        for rp2 in [false, true] {
            words(&mut m, 0x3400, &[126, 1, 1, 0, 3]);
            call(
                &mut b,
                &mut m,
                if rp2 { 122 } else { 66 },
                &[
                    dev,
                    37,
                    1,
                    0,
                    0,
                    2,
                    0,
                    0,
                    OUT,
                    if depth { 0x3400 } else { 0 },
                ],
            );
            let pass = word(&m, OUT);
            m.write(0x3500, &37u32.to_le_bytes()).unwrap();
            m.write(0x3504, &126u32.to_le_bytes()).unwrap();
            words(
                &mut m,
                0x3300,
                &[0, 17, 16, 16, 1, 0x3500, 1, 0, 32, 16, 16, 1, 0x3504, 1],
            );
            call(
                &mut b,
                &mut m,
                125,
                &[dev, pass, 16, 16, if depth { 2 } else { 1 }, 0x3300, OUT],
            );
            let fb = word(&m, OUT);
            // Exceed the table capacity with post-preflight write failures.
            // Unpublished framebuffer/renderpass objects must be rolled back.
            if depth && rp2 {
                m.map_backed(
                    0xa000,
                    vec![Rc::new(FailedOutput(RefCell::new([0; PAGE_SIZE as usize])))],
                    true,
                    Permissions::RW,
                    false,
                )
                .unwrap();
                for _ in 0..260 {
                    for (op, args) in [
                        (125, vec![dev, pass, 16, 16, 2, 0x3300, 0xa000]),
                        (122, vec![dev, 37, 1, 0, 0, 2, 0, 0, 0xa000]),
                    ] {
                        let mut bytes = [0; 128];
                        for (i, v) in args.iter().enumerate() {
                            bytes[i * 8..i * 8 + 8].copy_from_slice(&v.to_le_bytes());
                        }
                        m.write(PACKET, &bytes).unwrap();
                        assert_eq!(
                            b.invoke(&mut m, op, PACKET, 128),
                            Err(MemoryError::BackingIo(5))
                        );
                        assert_eq!(word(&m, 0xa000), 0);
                    }
                }
                m.unmap(0xa000, PAGE_SIZE).unwrap();
            }
            for color in [[1f32, 0., 0., 1.], [0., 1., 0., 1.]] {
                let (img, view, backing) = image(&mut b, &mut m, dev, 37, 17);
                let z = depth.then(|| image(&mut b, &mut m, dev, 126, 32));
                call(&mut b, &mut m, 38, &[dev, cmd, 0]);
                words(&mut m, 0x3600, &[view, z.map_or(0, |x| x.1)]);
                for (i, value) in color.iter().chain([0.5f32, 0., 0., 0.].iter()).enumerate() {
                    m.write(0x3700 + i as u64 * 4, &value.to_bits().to_le_bytes())
                        .unwrap();
                }
                // Missing late bindings and overflowing render areas are rejected
                // without changing command state, then the same buffer records.
                let begin = [
                    dev,
                    cmd,
                    pass,
                    fb,
                    0,
                    0,
                    16,
                    16,
                    0x3700,
                    if depth { 2 } else { 1 },
                    0x3600,
                    if depth { 2 } else { 1 },
                ];
                let mut bad = begin;
                bad[11] = 0;
                assert_eq!(
                    request(&mut b, &mut m, if rp2 { 123 } else { 71 }, &bad),
                    (-13i64) as u64
                );
                bad = begin;
                bad[6] = 17;
                assert_eq!(
                    request(&mut b, &mut m, if rp2 { 123 } else { 71 }, &bad),
                    (-13i64) as u64
                );
                call(&mut b, &mut m, if rp2 { 123 } else { 71 }, &begin);
                call(&mut b, &mut m, if rp2 { 124 } else { 72 }, &[dev, cmd]);
                words(&mut m, 0x3800, &[img, 256, 2048, 2, 6, 1]);
                call(
                    &mut b,
                    &mut m,
                    44,
                    &[dev, cmd, 1024, 4096, 0, 0, 0x3800, 1, 6],
                );
                words(&mut m, 0x3900, &[0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 16, 16, 1]);
                call(&mut b, &mut m, 74, &[dev, cmd, img, 6, buffer, 0x3900, 1]);
                call(
                    &mut b,
                    &mut m,
                    44,
                    &[dev, cmd, 4096, 16384, 4096, 8192, 0, 0, 6],
                );
                call(&mut b, &mut m, 39, &[dev, cmd]);
                words(&mut m, 0x3a00, &[cmd]);
                words(&mut m, 0x3b00, &[0, 0, 0, 0x3a00, 1, 0, 0]);
                call(&mut b, &mut m, 56, &[dev, queue, 0x3b00, 1, fence]);
                // Exercise the bridge's deferred cleanup: guest handle removal
                // cannot destroy attachments still owned by native commands.
                call(&mut b, &mut m, 65, &[dev, view]);
                call(&mut b, &mut m, 61, &[dev, img]);
                call(&mut b, &mut m, 15, &[dev, backing]);
                if let Some((img, view, backing)) = z {
                    call(&mut b, &mut m, 65, &[dev, view]);
                    call(&mut b, &mut m, 61, &[dev, img]);
                    call(&mut b, &mut m, 15, &[dev, backing]);
                }
                poll(&mut b, &mut m, 51, &[dev, fence], 1);
                let mut rgba = [0; 1024];
                m.read(0x4000, &mut rgba).unwrap();
                let expected: Vec<_> = color.iter().map(|c| (c * 255.) as u8).collect();
                assert!(rgba.chunks_exact(4).all(|p| p == expected));
                call(&mut b, &mut m, 53, &[dev, pool, 0]);
                call(&mut b, &mut m, 50, &[dev, fence]);
            }
            call(&mut b, &mut m, 69, &[dev, fb]);
            call(&mut b, &mut m, 67, &[dev, pass]);
        }
    }
    call(&mut b, &mut m, 49, &[dev, fence]);
    call(&mut b, &mut m, 35, &[dev, pool]);
    call(&mut b, &mut m, 18, &[dev, memory]);
    call(&mut b, &mut m, 12, &[dev, buffer]);
    call(&mut b, &mut m, 15, &[dev, memory]);
    call(&mut b, &mut m, 9, &[dev]);
    call(&mut b, &mut m, 2, &[instance]);
    println!("native-renderpass-ok: legacy/renderpass2, imageless rebinding, color/depth, feature/geometry guards, GPU readback and deferred attachment cleanup");
}

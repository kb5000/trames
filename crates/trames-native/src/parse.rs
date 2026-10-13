// SPDX-License-Identifier: GPL-3.0-or-later
//! Pinned musl floatscan over a bounded host slice; native binary64 intermediates.
#[repr(C)]
#[derive(Default, Debug)]
struct RawResult {
    bits: u64,
    consumed: u64,
    error: i32,
    exhausted: i32,
}
unsafe extern "C" {
    fn trames_native_parse_float(
        bytes: *const u8,
        len: usize,
        precision: i32,
        result: *mut RawResult,
    );
}
#[derive(Debug)]
pub struct ParsedFloat {
    pub bits: u64,
    pub consumed: usize,
    pub errno: i32,
    pub needs_more: bool,
}
pub fn floating(bytes: &[u8], single: bool) -> ParsedFloat {
    let mut raw = RawResult::default();
    // SAFETY: The private cursor reads only within this slice. Its pushback is
    // an index, never an unchecked pointer. Result layout is shared with driver.c;
    // native errno/FP state is restored before returning and no pointer escapes.
    unsafe {
        trames_native_parse_float(bytes.as_ptr(), bytes.len(), i32::from(!single), &mut raw);
    }
    ParsedFloat {
        bits: raw.bits,
        consumed: raw.consumed as usize,
        errno: raw.error,
        needs_more: raw.exhausted != 0,
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn restores_ambient_host_rounding_status_and_errno() {
        #[cfg(target_vendor = "apple")]
        unsafe extern "C" {
            fn __error() -> *mut i32;
        }
        #[cfg(not(target_vendor = "apple"))]
        unsafe extern "C" {
            fn __errno_location() -> *mut i32;
        }
        // SAFETY: Thread-local errno and this test thread's FP control/status.
        // Restore their original state before any assertion or test return.
        unsafe {
            #[cfg(target_vendor = "apple")]
            let errno = __error();
            #[cfg(not(target_vendor = "apple"))]
            let errno = __errno_location();
            let original_errno = *errno;
            *errno = 13;
            #[cfg(target_arch = "aarch64")]
            let (old_fpcr, old_fpsr, wanted_fpcr, wanted_fpsr) = {
                let (old_fpcr, old_fpsr): (u64, u64);
                core::arch::asm!("mrs {0}, fpcr", "mrs {1}, fpsr", out(reg) old_fpcr, out(reg) old_fpsr);
                let wanted_fpcr = (old_fpcr & !0xc00000) | 0xc00000;
                let wanted_fpsr = old_fpsr | 8;
                core::arch::asm!("msr fpcr, {0}", "msr fpsr, {1}", in(reg) wanted_fpcr, in(reg) wanted_fpsr);
                (old_fpcr, old_fpsr, wanted_fpcr, wanted_fpsr)
            };
            #[cfg(target_arch = "x86_64")]
            let (old_csr, wanted_csr) = {
                let mut old_csr = 0u32;
                core::arch::asm!("stmxcsr [{0}]", in(reg) &mut old_csr);
                let wanted_csr = (old_csr & !0x6000) | 0x6004;
                core::arch::asm!("ldmxcsr [{0}]", in(reg) &wanted_csr);
                (old_csr, wanted_csr)
            };
            let result = floating(b"1.25!\0", false);
            let after_errno = *errno;
            *errno = original_errno;
            #[cfg(target_arch = "aarch64")]
            let state_matches = {
                let (after_fpcr, after_fpsr): (u64, u64);
                core::arch::asm!("mrs {0}, fpcr", "mrs {1}, fpsr", out(reg) after_fpcr, out(reg) after_fpsr);
                core::arch::asm!("msr fpcr, {0}", "msr fpsr, {1}", in(reg) old_fpcr, in(reg) old_fpsr);
                (after_fpcr, after_fpsr) == (wanted_fpcr, wanted_fpsr)
            };
            #[cfg(target_arch = "x86_64")]
            let state_matches = {
                let mut after_csr = 0u32;
                core::arch::asm!("stmxcsr [{0}]", in(reg) &mut after_csr);
                core::arch::asm!("ldmxcsr [{0}]", in(reg) &old_csr);
                after_csr == wanted_csr
            };
            assert!(state_matches);
            assert_eq!(after_errno, 13);
            assert_eq!(result.bits, 1.25f64.to_bits());
            assert_eq!(result.consumed, 4);
        }
    }
    #[test]
    fn bounded_cursor_handles_decimal_hex_special_values_and_incomplete_tokens() {
        for (text, end, value) in [
            ("123.5tail", 5, 123.5),
            (" \t-0x1.8p+3!", 11, -12.0),
            ("1e+z", 1, 1.0),
            ("12x", 2, 12.0),
            ("-0", 2, -0.0),
        ] {
            let input = format!("{text}\0");
            let p = floating(input.as_bytes(), false);
            assert!(!p.needs_more, "{text}");
            assert_eq!(p.consumed, end, "{text}");
            assert_eq!(p.bits, f64::to_bits(value), "{text}");
            assert_eq!(p.errno, 0, "{text}");
        }
        let p = floating(b"invalid\0", false);
        assert_eq!((p.bits, p.consumed, p.errno), (0, 0, 22));
        assert!(floating(b"1234", false).needs_more);
        assert!(!floating(b"12x", false).needs_more);
        for single in [false, true] {
            let p = floating(b"nan(payload)!\0", single);
            assert_eq!(p.consumed, 12);
            assert!(!p.needs_more);
            let p = floating(b"-infinity!\0", single);
            assert_eq!(p.consumed, 9);
            assert_eq!(p.errno, 0);
            let p = floating(b"1e10000!\0", single);
            assert_eq!(p.errno, 34);
        }
    }
}

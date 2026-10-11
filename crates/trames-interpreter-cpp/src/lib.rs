// SPDX-License-Identifier: GPL-3.0-or-later
//! The sole unsafe C++ boundary. No guest address is passed as a host pointer.
pub const MAX_WORDS: usize = 64;
#[repr(C)]
#[derive(Default, Clone, Debug)]
pub struct State {
    pub x: [u64; 31],
    pub sp: u64,
    pub pc: u64,
    pub nzcv: u32,
    pub reserved: u32,
}
#[repr(C)]
#[derive(Debug)]
pub struct Result {
    pub instructions: u64,
    pub reason: u32,
    pub reserved: u32,
}
extern "C" {
    fn trames_cpp_run(state: *mut State, words: *const u32, count: u32, budget: u32) -> Result;
    fn trames_cpp_abi_version() -> u32;
}
pub fn abi_version() -> u32 {
    // SAFETY: a pure, parameterless function in the statically compiled engine.
    unsafe { trames_cpp_abi_version() }
}
pub fn run(state: &mut State, words: &[u32], budget: u64) -> Result {
    assert!(!words.is_empty() && words.len() <= MAX_WORDS && budget <= 4096);
    // SAFETY: repr(C) PODs match the checked header. References guarantee live,
    // initialized, disjoint buffers; the native engine validates count, budget
    // and decoded indices, accesses no guest memory and retains no pointers.
    let result =
        unsafe { trames_cpp_run(state, words.as_ptr(), words.len() as u32, budget as u32) };
    assert!(result.instructions <= budget && result.reason <= 2 && state.nzcv <= 15);
    result
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn abi_layout_and_zero_budget_have_no_effects() {
        assert_eq!(abi_version(), 1);
        assert_eq!(std::mem::size_of::<State>(), 272);
        assert_eq!(std::mem::size_of::<Result>(), 16);
        let mut s = State {
            pc: 0x1000,
            ..State::default()
        };
        let r = run(&mut s, &[0xd503201f], 0);
        assert_eq!(r.instructions, 0);
        assert_eq!(s.pc, 0x1000);
        let r = run(&mut s, &[0xffffffff], 1);
        assert_eq!(r.instructions, 0);
        assert_eq!(r.reason, 2);
        assert_eq!(s.pc, 0x1000);
    }
}

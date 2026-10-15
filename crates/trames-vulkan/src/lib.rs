// SPDX-License-Identifier: GPL-3.0-or-later
//! Experimental, headless Vulkan compute over a separately negotiated NativeCall.
pub const PROFILE: u64 = 0x0001_0006;
pub const AUXV_KEY: u64 = 0x5452_0008;
mod backend;
pub use backend::{Backend, Invocation, TransferMetrics};
mod budget;
mod task;
pub use budget::Budget;

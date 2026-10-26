// SPDX-License-Identifier: GPL-3.0-or-later
//! iPad/interpreter composition of the common Linux process kernel.
pub use trames_session::{
    Exit, MemoryLimits, RuntimeError, RuntimeMetrics, RuntimeOptions, Terminal, VulkanDisplay,
    DEFAULT_INSTRUCTION_LIMIT, STACK_SIZE, STACK_TOP,
};
pub type Runtime = trames_session::Runtime<trames_cpu::Cpu>;

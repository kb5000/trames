/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TRAMES_CPU_H
#define TRAMES_CPU_H
#if defined(__GNUC__) || defined(__clang__)
typedef __UINT64_TYPE__ trames_u64;
typedef __UINT32_TYPE__ trames_u32;
typedef __UINT8_TYPE__ trames_u8;
typedef __INT64_TYPE__ trames_i64;
typedef __INT32_TYPE__ trames_i32;
#else
#include <stdint.h>
typedef uint64_t trames_u64;
typedef uint32_t trames_u32;
typedef uint8_t trames_u8;
typedef int64_t trames_i64;
typedef int32_t trames_i32;
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* ABI 1: no memory pointers derived from guest addresses. The caller supplies
 * a checked contiguous executable block, <=64 words, and budget <=4096.
 * Only integer state is modified. Reasons: 0=yield/side exit, 1=SVC, 2=fallback.
 * Unknown/unallocated words return before effects; Rust implements the fallback.
 * Caller owns all POD buffers during this synchronous call, no retained pointers.
 */
typedef struct { trames_u64 x[31], sp, pc; trames_u32 nzcv, reserved; } TramesCpuState;
typedef struct { trames_u64 instructions; trames_u32 reason, reserved; } TramesCpuResult;
trames_u32 trames_cpp_abi_version(void);
TramesCpuResult trames_cpp_run(TramesCpuState *state, const trames_u32 *words, trames_u32 count, trames_u32 budget);
#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TRAMES_H
#define TRAMES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* ABI v3 (single production interpreter): caller-owned byte buffers, max 65536 bytes, no retained pointers.
 * Null allowed only with length/capacity 0. UTF-8 JSON and errors have no NUL.
 * Session handles are process-wide, thread-safe, monotonic and never reused.
 * All fallible int32 calls return -1 on failure; read trames_last_error on the
 * same calling thread. Open returns 0 on failure. Close cancels asynchronously.
 * At most 8 workers (including workers finishing cancellation), 64 KiB queues.
 * Input/output return bytes accepted/drained, including 0 for backpressure.
 * Only transfer blocks; run it on a background queue. No callbacks into Swift.
 */
uint32_t trames_abi_version(void);
int32_t trames_environment_install(const uint8_t *json, uint32_t length);
/* Destructive native-only store reset, {"store":"/authorized/private/store"}.
 * Serialize against new opens; returns 1 while a worker owns the lock, 0 after
 * deletion, -1 on error. Call on a background queue after closing sessions. */
int32_t trames_environment_reset(const uint8_t *json, uint32_t length);
uint64_t trames_session_open(const uint8_t *json, uint32_t length);
int32_t trames_session_input(uint64_t handle, const uint8_t *bytes, uint32_t length);
int32_t trames_session_output(uint64_t handle, uint8_t *bytes, uint32_t capacity);
int32_t trames_session_status(uint64_t handle, uint8_t *json, uint32_t capacity);
int32_t trames_session_resize(uint64_t handle, uint16_t rows, uint16_t columns);
int32_t trames_session_pause(uint64_t handle, uint32_t paused);
/* Reserve a single-use transfer token before dispatching/cancelling a copy.
 * Cancel affects only that copy, including one not yet enqueued. Token 0 in a
 * transfer request preserves the synchronous ABI. At most 8 reserved tickets.
 * A request consumes its ticket even on path/cancellation errors. */
uint64_t trames_session_transfer_begin(uint64_t handle);
int32_t trames_session_transfer_cancel(uint64_t handle, uint64_t token);
int32_t trames_session_transfer(uint64_t handle, const uint8_t *json, uint32_t length);
/* Bridge v1 NDJSON, max 32768 bytes/frame, 16 connections, 32 native requests
 * per session (8 per connection), 120 second consent deadline. Poll returns
 * 0 for no request; capacity errors do not claim it. Native effects require
 * explicit frontend consent. Replies reject expired/cancelled/closed tickets.
 * active returns 1/0, or -1 for an invalid handle. No callbacks into Swift. */
int32_t trames_session_bridge_poll(uint64_t handle, uint8_t *json, uint32_t capacity);
int32_t trames_session_bridge_reply(uint64_t handle, const uint8_t *json, uint32_t length);
int32_t trames_session_bridge_active(uint64_t handle, uint64_t ticket);
int32_t trames_session_close(uint64_t handle);
int32_t trames_last_error(uint8_t *bytes, uint32_t capacity);
#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Private versioned LP64 continuation, shared with trames-accel/callbacks.rs. */
#include <stdint.h>
#define TRAMES_CALLBACK_VERSION 0x54525301UL
#define TRAMES_SORT_WORDS 240
#define TRAMES_SEARCH_WORDS 16
#define TRAMES_CB_KIND 1
#define TRAMES_CB_BASE 3
#define TRAMES_CB_NEL 4
#define TRAMES_CB_WIDTH 5
#define TRAMES_CB_KEY 6
#define TRAMES_CB_LEFT 7
#define TRAMES_CB_RIGHT 8
#define TRAMES_CB_RESULT 9
struct trames_callback_result { uintptr_t event, fallback; };
extern struct trames_callback_result trames_callback_step(uintptr_t, uintptr_t, uintptr_t);

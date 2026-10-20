/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TRAMES_XWAYLAND_NATIVE_WSI_H
#define TRAMES_XWAYLAND_NATIVE_WSI_H
#include "misc.h"
#include "xwayland-types.h"
Bool xwl_native_requested(void);
Bool xwl_native_init(struct xwl_screen *screen);
Bool xwl_native_present(struct xwl_window *window);
void xwl_native_window_destroy(struct xwl_window *window);
void xwl_native_fini(struct xwl_screen *screen);
#endif

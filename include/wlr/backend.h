#ifndef WLR_BACKEND_H
#define WLR_BACKEND_H

#include <wayland-server.h>
#include <wlr/backend/session.h>
#include <wlr/render/egl.h>

struct wlr_backend_impl;

struct wlr_backend {
	const struct wlr_backend_impl *impl;

	struct {
		/** Raised when a wlr_input_device is added. */
		struct wl_signal input_add;
		/** Raised when a wlr_input_device is removed. */
		struct wl_signal input_remove;
		/** Raised when a wlr_output is added. */
		struct wl_signal output_add;
		/** Raised when a wlr_output is removed. */
		struct wl_signal output_remove;
	} events;
};

/**
 * Examines the runtime environment and creates the most suitable backend.
 */
struct wlr_backend *wlr_backend_autocreate(struct wl_display *display);

bool wlr_backend_start(struct wlr_backend *backend);

void wlr_backend_destroy(struct wlr_backend *backend);

/**
 * Returns the wlr_egl object for this backend, if applicable, or NULL
 * otherwise.
 */
struct wlr_egl *wlr_backend_get_egl(struct wlr_backend *backend);

#endif

#ifndef WLR_BACKEND_H
#define WLR_BACKEND_H

#include <wayland-server.h>
#include <wlr/backend/session.h>
#include <wlr/render/egl.h>

struct wlr_backend_impl;

struct wlr_backend {
	const struct wlr_backend_impl *impl;

	struct {
		struct wl_signal input_add;
		struct wl_signal input_remove;
		struct wl_signal output_add;
		struct wl_signal output_remove;
	} events;
};

struct wlr_backend *wlr_backend_autocreate(struct wl_display *display);
bool wlr_backend_start(struct wlr_backend *backend);
void wlr_backend_destroy(struct wlr_backend *backend);
struct wlr_egl *wlr_backend_get_egl(struct wlr_backend *backend);

#endif

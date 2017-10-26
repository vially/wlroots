#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/types/wlr_list.h>
#include <wlr/util/log.h>
#include <GLES2/gl2.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/render.h>

static void wl_output_send_to_resource(struct wl_resource *resource) {
	assert(resource);
	struct wlr_output *output = wl_resource_get_user_data(resource);
	assert(output);
	const uint32_t version = wl_resource_get_version(resource);
	if (version >= WL_OUTPUT_GEOMETRY_SINCE_VERSION) {
		wl_output_send_geometry(resource, output->lx, output->ly,
			output->phys_width, output->phys_height, output->subpixel,
			output->make, output->model, output->transform);
	}
	if (version >= WL_OUTPUT_MODE_SINCE_VERSION) {
		struct wlr_output_mode *mode;
		wl_list_for_each(mode, &output->modes, link) {
			uint32_t flags = mode->flags & ~WL_OUTPUT_MODE_PREFERRED;
			if (output->current_mode == mode) {
				flags |= WL_OUTPUT_MODE_CURRENT;
			}
			wl_output_send_mode(resource, flags, mode->width, mode->height,
				mode->refresh);
		}

		if (wl_list_length(&output->modes) == 0) {
			// Output has no mode, send the current width/height
			wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT,
				output->width, output->height, 0);
		}
	}
	if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
		wl_output_send_scale(resource, output->scale);
	}
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
		wl_output_send_done(resource);
	}
}

static void wlr_output_send_current_mode_to_resource(
		struct wl_resource *resource) {
	struct wlr_output *output = wl_resource_get_user_data(resource);
	assert(output);
	const uint32_t version = wl_resource_get_version(resource);
	if (version < WL_OUTPUT_MODE_SINCE_VERSION) {
		return;
	}
	if (output->current_mode != NULL) {
		struct wlr_output_mode *mode = output->current_mode;
		wl_output_send_mode(resource, mode->flags | WL_OUTPUT_MODE_CURRENT,
			mode->width, mode->height, mode->refresh);
	} else {
		// Output has no mode, send the current width/height
		wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, output->width,
			output->height, 0);
	}
}

static void wl_output_destroy(struct wl_resource *resource) {
	struct wlr_output *output = wl_resource_get_user_data(resource);
	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &output->wl_resources) {
		if (_resource == resource) {
			struct wl_list *link = wl_resource_get_link(_resource);
			wl_list_remove(link);
			break;
		}
	}
}

static void wl_output_release(struct wl_client *client, struct wl_resource *resource) {
	wl_output_destroy(resource);
}

static struct wl_output_interface wl_output_impl = {
	.release = wl_output_release
};

static void wl_output_bind(struct wl_client *wl_client, void *_wlr_output,
		uint32_t version, uint32_t id) {
	struct wlr_output *wlr_output = _wlr_output;
	assert(wl_client && wlr_output);

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&wl_output_interface, version, id);
	wl_resource_set_implementation(wl_resource, &wl_output_impl, wlr_output,
		wl_output_destroy);
	wl_list_insert(&wlr_output->wl_resources,
		wl_resource_get_link(wl_resource));
	wl_output_send_to_resource(wl_resource);
}

struct wl_global *wlr_output_create_global(struct wlr_output *wlr_output,
		struct wl_display *display) {
	if (wlr_output->wl_global != NULL) {
		return wlr_output->wl_global;
	}
	struct wl_global *wl_global = wl_global_create(display,
		&wl_output_interface, 3, wlr_output, wl_output_bind);
	wlr_output->wl_global = wl_global;
	wl_list_init(&wlr_output->wl_resources);
	return wl_global;
}

void wlr_output_destroy_global(struct wlr_output *wlr_output) {
	if (wlr_output->wl_global == NULL) {
		return;
	}
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &wlr_output->wl_resources) {
		struct wl_list *link = wl_resource_get_link(resource);
		wl_list_remove(link);
	}
	wl_global_destroy(wlr_output->wl_global);
	wlr_output->wl_global = NULL;
}

static void wlr_output_update_matrix(struct wlr_output *output) {
	wlr_matrix_texture(output->transform_matrix, output->width, output->height,
		output->transform);
}

void wlr_output_enable(struct wlr_output *output, bool enable) {
	output->impl->enable(output, enable);
}

bool wlr_output_set_mode(struct wlr_output *output,
		struct wlr_output_mode *mode) {
	if (!output->impl || !output->impl->set_mode) {
		return false;
	}
	bool result = output->impl->set_mode(output, mode);
	if (result) {
		wlr_output_update_matrix(output);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &output->wl_resources) {
			wlr_output_send_current_mode_to_resource(resource);
		}
	}
	return result;
}

void wlr_output_update_size(struct wlr_output *output, int32_t width,
		int32_t height) {
	output->width = width;
	output->height = height;
	wlr_output_update_matrix(output);
	if (output->wl_global != NULL) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &output->wl_resources) {
			wlr_output_send_current_mode_to_resource(resource);
		}
	}
}

void wlr_output_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	output->impl->transform(output, transform);
	wlr_output_update_matrix(output);
}

void wlr_output_set_position(struct wlr_output *output, int32_t lx,
		int32_t ly) {
	if (lx == output->lx && ly == output->ly) {
		return;
	}

	output->lx = lx;
	output->ly = ly;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->wl_resources) {
		wl_output_send_to_resource(resource);
	}
}

static bool set_cursor(struct wlr_output *output, const uint8_t *buf,
		int32_t stride, uint32_t width, uint32_t height, int32_t hotspot_x,
		int32_t hotspot_y) {
	if (output->impl->set_cursor
			&& output->impl->set_cursor(output, buf, stride, width, height,
				hotspot_x, hotspot_y, true)) {
		output->cursor.is_sw = false;
		return true;
	}

	wlr_log(L_INFO, "Falling back to software cursor");

	output->cursor.is_sw = true;
	output->cursor.width = width;
	output->cursor.height = height;

	if (!output->cursor.renderer) {
		output->cursor.renderer = wlr_gles2_renderer_create(output->backend);
		if (!output->cursor.renderer) {
			return false;
		}
	}

	if (!output->cursor.texture) {
		output->cursor.texture =
			wlr_render_texture_create(output->cursor.renderer);
		if (!output->cursor.texture) {
			return false;
		}
	}

	return wlr_texture_upload_pixels(output->cursor.texture,
		WL_SHM_FORMAT_ARGB8888, stride, width, height, buf);
}

bool wlr_output_set_cursor(struct wlr_output *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height,
		int32_t hotspot_x, int32_t hotspot_y) {
	if (output->cursor.surface) {
		wl_list_remove(&output->cursor.surface_commit.link);
		wl_list_remove(&output->cursor.surface_destroy.link);
		output->cursor.surface = NULL;
	}

	output->cursor.hotspot_x = hotspot_x;
	output->cursor.hotspot_y = hotspot_y;

	return set_cursor(output, buf, stride, width, height, hotspot_x, hotspot_y);
}

static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

static void commit_cursor_surface(struct wlr_output *output,
		struct wlr_surface *surface) {
	if (output->cursor.is_sw) {
		return;
	}

	struct wl_shm_buffer *buffer = wl_shm_buffer_get(surface->current->buffer);
	if (buffer == NULL) {
		return;
	}

	uint32_t format = wl_shm_buffer_get_format(buffer);
	if (format != WL_SHM_FORMAT_ARGB8888) {
		return;
	}

	void *buffer_data = wl_shm_buffer_get_data(buffer);
	int32_t width = wl_shm_buffer_get_width(buffer);
	int32_t height = wl_shm_buffer_get_height(buffer);
	int32_t stride = wl_shm_buffer_get_stride(buffer);
	wl_shm_buffer_begin_access(buffer);
	wlr_output_set_cursor(output, buffer_data, stride/4, width, height,
		output->cursor.hotspot_x - surface->current->sx,
		output->cursor.hotspot_y - surface->current->sy);
	wl_shm_buffer_end_access(buffer);
}

static void handle_cursor_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_output *output = wl_container_of(listener, output,
		cursor.surface_commit);
	struct wlr_surface *surface = data;

	commit_cursor_surface(output, surface);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	struct wlr_frame_callback *cb, *cnext;
	wl_list_for_each_safe(cb, cnext, &surface->current->frame_callback_list,
			link) {
		wl_callback_send_done(cb->resource, timespec_to_msec(&now));
		wl_resource_destroy(cb->resource);
	}
}

static void handle_cursor_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output *output = wl_container_of(listener, output,
		cursor.surface_destroy);

	wl_list_remove(&output->cursor.surface_commit.link);
	wl_list_remove(&output->cursor.surface_destroy.link);
	output->cursor.surface = NULL;
}

void wlr_output_set_cursor_surface(struct wlr_output *output,
		struct wlr_surface *surface, int32_t hotspot_x, int32_t hotspot_y) {
	if (surface && strcmp(surface->role, "wl_pointer-cursor") != 0) {
		return;
	}

	output->cursor.hotspot_x = hotspot_x;
	output->cursor.hotspot_y = hotspot_y;

	if (surface && surface == output->cursor.surface) {
		if (output->impl->set_cursor && !output->cursor.is_sw) {
			// Only update the hotspot
			output->impl->set_cursor(output, NULL, 0, 0, 0, hotspot_x,
				hotspot_y, false);
		}
		return;
	}

	if (output->cursor.surface) {
		wl_list_remove(&output->cursor.surface_commit.link);
		wl_list_remove(&output->cursor.surface_destroy.link);
		output->cursor.surface = NULL;
	}

	// Disable hardware cursor
	// TODO: support hardware cursors
	output->cursor.is_sw = true;
	if (output->impl->set_cursor) {
		output->impl->set_cursor(output, NULL, 0, 0, 0, hotspot_x, hotspot_y,
			true);
	}

	//output->cursor.is_sw = output->impl->set_cursor == NULL;
	output->cursor.surface = surface;

	if (surface != NULL) {
		wl_signal_add(&surface->events.commit, &output->cursor.surface_commit);
		wl_signal_add(&surface->events.destroy,
			&output->cursor.surface_destroy);
		commit_cursor_surface(output, surface);
	} else {
		set_cursor(output, NULL, 0, 0, 0, hotspot_x, hotspot_y);
	}
}

bool wlr_output_move_cursor(struct wlr_output *output, int x, int y) {
	output->cursor.x = x;
	output->cursor.y = y;

	if (output->cursor.is_sw) {
		return true;
	}

	if (!output->impl->move_cursor) {
		return false;
	}

	return output->impl->move_cursor(output, x, y);
}

void wlr_output_init(struct wlr_output *output, struct wlr_backend *backend,
		const struct wlr_output_impl *impl) {
	output->backend = backend;
	output->impl = impl;
	wl_list_init(&output->modes);
	output->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	output->scale = 1;
	wl_signal_init(&output->events.frame);
	wl_signal_init(&output->events.swap_buffers);
	wl_signal_init(&output->events.resolution);
	wl_signal_init(&output->events.destroy);

	wl_list_init(&output->cursor.surface_commit.link);
	output->cursor.surface_commit.notify = handle_cursor_surface_commit;
	wl_list_init(&output->cursor.surface_destroy.link);
	output->cursor.surface_destroy.notify = handle_cursor_surface_destroy;
}

void wlr_output_destroy(struct wlr_output *output) {
	if (!output) {
		return;
	}

	wl_signal_emit(&output->events.destroy, output);

	wlr_texture_destroy(output->cursor.texture);
	wlr_renderer_destroy(output->cursor.renderer);

	struct wlr_output_mode *mode, *tmp_mode;
	wl_list_for_each_safe(mode, tmp_mode, &output->modes, link) {
		free(mode);
	}
	wl_list_remove(&output->modes);
	if (output->impl && output->impl->destroy) {
		output->impl->destroy(output);
	} else {
		free(output);
	}
}

void wlr_output_effective_resolution(struct wlr_output *output,
		int *width, int *height) {
	if (output->transform % 2 == 1) {
		*width = output->height;
		*height = output->width;
	} else {
		*width = output->width;
		*height = output->height;
	}
}

void wlr_output_make_current(struct wlr_output *output) {
	output->impl->make_current(output);
}

void wlr_output_swap_buffers(struct wlr_output *output) {
	if (output->cursor.is_sw) {
		glViewport(0, 0, output->width, output->height);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		struct wlr_texture *texture = output->cursor.texture;
		struct wlr_renderer *renderer = output->cursor.renderer;
		if (output->cursor.surface) {
			texture = output->cursor.surface->texture;
			renderer = output->cursor.surface->renderer;
		}

		// We check texture->valid because some clients set a cursor surface
		// with a NULL buffer to hide it
		if (renderer && texture && texture->valid) {
			float matrix[16];
			if (output->cursor.surface) {
				float translation[16];
				wlr_matrix_translate(&translation,
						output->cursor.x, output->cursor.y, 0);
				wlr_surface_get_matrix(output->cursor.surface,
						&matrix, &output->transform_matrix, &translation);
			} else {
				wlr_texture_get_matrix(texture,
						&matrix, &output->transform_matrix,
						output->cursor.x, output->cursor.y);
			}
			wlr_render_with_matrix(renderer, texture, &matrix);
		}
	}

	wl_signal_emit(&output->events.swap_buffers, &output);

	output->impl->swap_buffers(output);
}

void wlr_output_set_gamma(struct wlr_output *output,
	uint32_t size, uint16_t *r, uint16_t *g, uint16_t *b) {
	if (output->impl->set_gamma) {
		output->impl->set_gamma(output, size, r, g, b);
	}
}

uint32_t wlr_output_get_gamma_size(struct wlr_output *output) {
	if (!output->impl->get_gamma_size) {
		return 0;
	}
	return output->impl->get_gamma_size(output);
}

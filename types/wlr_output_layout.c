#include <wlr/util/log.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_box.h>
#include <limits.h>
#include <float.h>
#include <stdlib.h>
#include <assert.h>

struct wlr_output_layout_state {
	struct wlr_box *_box;
};

struct wlr_output_layout_output_state {
	struct wlr_output_layout *layout;
	struct wlr_output_layout_output *l_output;

	struct wlr_box *_box;
	bool auto_configured;

	struct wl_listener resolution;
	struct wl_listener output_destroy;
};

struct wlr_output_layout *wlr_output_layout_create() {
	struct wlr_output_layout *layout =
		calloc(1, sizeof(struct wlr_output_layout));
	layout->state = calloc(1, sizeof(struct wlr_output_layout_state));
	layout->state->_box = calloc(1, sizeof(struct wlr_box));
	wl_list_init(&layout->outputs);

	wl_signal_init(&layout->events.change);
	wl_signal_init(&layout->events.destroy);

	return layout;
}

static void wlr_output_layout_output_destroy(
		struct wlr_output_layout_output *l_output) {
	wl_list_remove(&l_output->state->resolution.link);
	wl_list_remove(&l_output->state->output_destroy.link);
	wl_list_remove(&l_output->link);
	free(l_output->state->_box);
	free(l_output->state);
	free(l_output);
}

void wlr_output_layout_destroy(struct wlr_output_layout *layout) {
	if (!layout) {
		return;
	}

	wl_signal_emit(&layout->events.destroy, layout);

	struct wlr_output_layout_output *_output, *temp = NULL;
	wl_list_for_each_safe(_output, temp, &layout->outputs, link) {
		wlr_output_layout_output_destroy(_output);
	}

	free(layout->state->_box);
	free(layout->state);
	free(layout);
}

static struct wlr_box *wlr_output_layout_output_get_box(
		struct wlr_output_layout_output *l_output) {
	l_output->state->_box->x = l_output->x;
	l_output->state->_box->y = l_output->y;
	int width, height;
	wlr_output_effective_resolution(l_output->output, &width, &height);
	l_output->state->_box->width = width;
	l_output->state->_box->height = height;
	return l_output->state->_box;
}

/**
 * This must be called whenever the layout changes to reconfigure the auto
 * configured outputs and emit the `changed` event.
 *
 * Auto configured outputs are placed to the right of the north east corner of
 * the rightmost output in the layout in a horizontal line.
 */
static void wlr_output_layout_reconfigure(struct wlr_output_layout *layout) {
	int max_x = INT_MIN;
	int max_x_y = INT_MIN; // y value for the max_x output

	// find the rightmost x coordinate occupied by a manually configured output
	// in the layout
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (l_output->state->auto_configured) {
			continue;
		}

		struct wlr_box *box = wlr_output_layout_output_get_box(l_output);
		if (box->x + box->width > max_x) {
			max_x = box->x + box->width;
			max_x_y = box->y;
		}
	}

	if (max_x == INT_MIN) {
		// there are no manually configured outputs
		max_x = 0;
		max_x_y = 0;
	}

	wl_list_for_each(l_output, &layout->outputs, link) {
		if (!l_output->state->auto_configured) {
			continue;
		}
		struct wlr_box *box = wlr_output_layout_output_get_box(l_output);
		l_output->x = max_x;
		l_output->y = max_x_y;
		max_x += box->width;
	}

	wl_list_for_each(l_output, &layout->outputs, link) {
		wlr_output_set_position(l_output->output, l_output->x, l_output->y);
	}

	wl_signal_emit(&layout->events.change, layout);
}

static void handle_output_resolution(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output_state *state =
		wl_container_of(listener, state, resolution);
	wlr_output_layout_reconfigure(state->layout);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output_state *state =
		wl_container_of(listener, state, output_destroy);
	struct wlr_output_layout *layout = state->layout;
	wlr_output_layout_output_destroy(state->l_output);
	wlr_output_layout_reconfigure(layout);
}

static struct wlr_output_layout_output *wlr_output_layout_output_create(
		struct wlr_output_layout *layout, struct wlr_output *output) {
	struct wlr_output_layout_output *l_output;
	l_output= calloc(1, sizeof(struct wlr_output_layout_output));
	l_output->state = calloc(1, sizeof(struct wlr_output_layout_output_state));
	l_output->state->l_output = l_output;
	l_output->state->_box = calloc(1, sizeof(struct wlr_box));
	l_output->state->layout = layout;
	l_output->output = output;
	wl_list_insert(&layout->outputs, &l_output->link);

	wl_signal_add(&output->events.resolution, &l_output->state->resolution);
	l_output->state->resolution.notify = handle_output_resolution;

	wl_signal_add(&output->events.destroy, &l_output->state->output_destroy);
	l_output->state->output_destroy.notify = handle_output_destroy;

	return l_output;
}

void wlr_output_layout_add(struct wlr_output_layout *layout,
		struct wlr_output *output, int x, int y) {
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(layout, output);
	if (!l_output) {
		l_output = wlr_output_layout_output_create(layout, output);
	}
	l_output->x = x;
	l_output->y = y;
	l_output->state->auto_configured = false;
	wlr_output_layout_reconfigure(layout);
}

struct wlr_output_layout_output *wlr_output_layout_get(
		struct wlr_output_layout *layout, struct wlr_output *reference) {
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (l_output->output == reference) {
			return l_output;
		}
	}
	return NULL;
}

bool wlr_output_layout_contains_point(struct wlr_output_layout *layout,
		struct wlr_output *reference, int x, int y) {
	if (reference) {
		struct wlr_output_layout_output *l_output =
			wlr_output_layout_get(layout, reference);
		struct wlr_box *box = wlr_output_layout_output_get_box(l_output);
		return wlr_box_contains_point(box, x, y);
	} else {
		return !!wlr_output_layout_output_at(layout, x, y);
	}
}

bool wlr_output_layout_intersects(struct wlr_output_layout *layout,
		struct wlr_output *reference, int x1, int y1, int x2, int y2) {
	struct wlr_output_layout_output *layout_output =
		wlr_output_layout_get(layout, reference);
	if (!layout_output) {
		return false;
	}

	struct wlr_box *output_box = wlr_output_layout_output_get_box(layout_output);
	struct wlr_box target_box = {x1, y1, x2 - x1, y2 - y1};

	struct wlr_box out;
	struct wlr_box *out_ptr = &out;
	return wlr_box_intersection(output_box, &target_box, &out_ptr);
}

struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *layout,
		double x, double y) {
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (l_output->output) {
			struct wlr_box *box = wlr_output_layout_output_get_box(l_output);
			if (wlr_box_contains_point(box, x, y)) {
				return l_output->output;
			}
		}
	}
	return NULL;
}

void wlr_output_layout_move(struct wlr_output_layout *layout,
		struct wlr_output *output, int x, int y) {
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(layout, output);
	if (l_output) {
		l_output->x = x;
		l_output->y = y;
		l_output->state->auto_configured = false;
		wlr_output_layout_reconfigure(layout);
	} else {
		wlr_log(L_ERROR, "output not found in this layout: %s", output->name);
	}
}

void wlr_output_layout_remove(struct wlr_output_layout *layout,
		struct wlr_output *output) {
	struct wlr_output_layout_output *l_output;
	l_output= wlr_output_layout_get(layout, output);
	if (l_output) {
		wlr_output_layout_output_destroy(l_output);
		wlr_output_layout_reconfigure(layout);
	}
}

void wlr_output_layout_output_coords(struct wlr_output_layout *layout,
		struct wlr_output *reference, double *x, double *y) {
	assert(layout && reference);
	double src_x = *x;
	double src_y = *y;

	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (l_output->output == reference) {
			*x = src_x - (double)l_output->x;
			*y = src_y - (double)l_output->y;
			return;
		}
	}
}

void wlr_output_layout_closest_point(struct wlr_output_layout *layout,
		struct wlr_output *reference, double x, double y, double *dest_x,
		double *dest_y) {
	double min_x = DBL_MAX, min_y = DBL_MAX, min_distance = DBL_MAX;
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (reference != NULL && reference != l_output->output) {
			continue;
		}

		double output_x, output_y, output_distance;
		struct wlr_box *box = wlr_output_layout_output_get_box(l_output);
		wlr_box_closest_point(box, x, y, &output_x, &output_y);

		// calculate squared distance suitable for comparison
		output_distance =
			(x - output_x) * (x - output_x) + (y - output_y) * (y - output_y);

		if (output_distance < min_distance) {
			min_x = output_x;
			min_y = output_y;
			min_distance = output_distance;
		}
	}

	*dest_x = min_x;
	*dest_y = min_y;
}

struct wlr_box *wlr_output_layout_get_box(
		struct wlr_output_layout *layout, struct wlr_output *reference) {
	struct wlr_output_layout_output *l_output;
	if (reference) {
		// output extents
		l_output = wlr_output_layout_get(layout, reference);

		if (l_output) {
			return wlr_output_layout_output_get_box(l_output);
		} else {
			return NULL;
		}
	} else {
		// layout extents
		int min_x = INT_MAX, min_y = INT_MAX;
		int max_x = INT_MIN, max_y = INT_MIN;
		wl_list_for_each(l_output, &layout->outputs, link) {
			struct wlr_box *box = wlr_output_layout_output_get_box(l_output);

			if (box->x < min_x) {
				min_x = box->x;
			}
			if (box->y < min_y) {
				min_y = box->y;
			}
			if (box->x + box->width > max_x) {
				max_x = box->x + box->width;
			}
			if (box->y + box->height > max_y) {
				max_y = box->y + box->height;
			}
		}

		layout->state->_box->x = min_x;
		layout->state->_box->y = min_y;
		layout->state->_box->width = max_x - min_x;
		layout->state->_box->height = max_y - min_y;

		return layout->state->_box;
	}

	// not reached
}

void wlr_output_layout_add_auto(struct wlr_output_layout *layout,
		struct wlr_output *output) {
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(layout, output);

	if (!l_output) {
		l_output = wlr_output_layout_output_create(layout, output);
	}

	l_output->state->auto_configured = true;
	wlr_output_layout_reconfigure(layout);
}

struct wlr_output *wlr_output_layout_get_center_output(
		struct wlr_output_layout *layout) {
	if (wl_list_empty(&layout->outputs)) {
		return NULL;
	}

	struct wlr_box *extents = wlr_output_layout_get_box(layout, NULL);
	double center_x = extents->width / 2 + extents->x;
	double center_y = extents->height / 2 + extents->y;

	double dest_x = 0, dest_y = 0;
	wlr_output_layout_closest_point(layout, NULL, center_x, center_y,
		&dest_x, &dest_y);

	return wlr_output_layout_output_at(layout, dest_x, dest_y);
}

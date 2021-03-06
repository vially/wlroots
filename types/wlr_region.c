#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <wayland-server.h>
#include <pixman.h>

static void region_add(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	pixman_region32_t *region = wl_resource_get_user_data(resource);
	pixman_region32_union_rect(region, region, x, y, width, height);
}

static void region_subtract(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	pixman_region32_t *region = wl_resource_get_user_data(resource);
	pixman_region32_union_rect(region, region, x, y, width, height);

	pixman_region32_t rect;
	pixman_region32_init_rect(&rect, x, y, width, height);
	pixman_region32_subtract(region, region, &rect);
	pixman_region32_fini(&rect);
}

static void region_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wl_region_interface region_interface = {
	.destroy = region_destroy,
	.add = region_add,
	.subtract = region_subtract,
};

static void destroy_region(struct wl_resource *resource) {
	pixman_region32_t *reg = wl_resource_get_user_data(resource);
	pixman_region32_fini(reg);
	free(reg);
}

void wlr_region_create(struct wl_client *client, struct wl_resource *res,
		uint32_t id) {
	pixman_region32_t *region = calloc(1, sizeof(pixman_region32_t));
	if (region == NULL) {
		wl_resource_post_no_memory(res);
		return;
	}

	pixman_region32_init(region);

	struct wl_resource *region_resource = wl_resource_create(client,
		&wl_region_interface, 1, id);
	if (region_resource == NULL) {
		free(region);
		wl_resource_post_no_memory(res);
		return;
	}
	wl_resource_set_implementation(region_resource, &region_interface, region,
		destroy_region);
}

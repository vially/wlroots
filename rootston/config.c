#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/param.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_box.h>
#include "rootston/config.h"
#include "rootston/input.h"
#include "rootston/ini.h"

static void usage(const char *name, int ret) {
	fprintf(stderr,
		"usage: %s [-C <FILE>] [-E <COMMAND>]\n"
		"\n"
		" -C <FILE>      Path to the configuration file\n"
		"                (default: rootston.ini).\n"
		"                See `rootston.ini.example` for config\n"
		"                file documentation.\n"
		" -E <COMMAND>   Command that will be ran at startup.\n" , name);

	exit(ret);
}

static struct wlr_box *parse_geometry(const char *str) {
	// format: {width}x{height}+{x}+{y}
	if (strlen(str) > 255) {
		wlr_log(L_ERROR, "cannot parse geometry string, too long");
		return NULL;
	}

	char *buf = strdup(str);
	struct wlr_box *box = calloc(1, sizeof(struct wlr_box));

	bool has_width = false;
	bool has_height = false;
	bool has_x = false;
	bool has_y = false;

	char *pch = strtok(buf, "x+");
	while (pch != NULL) {
		errno = 0;
		char *endptr;
		long val = strtol(pch, &endptr, 0);

		if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
				(errno != 0 && val == 0)) {
			goto invalid_input;
		}

		if (endptr == pch) {
			goto invalid_input;
		}

		if (!has_width) {
			box->width = val;
			has_width = true;
		} else if (!has_height) {
			box->height = val;
			has_height = true;
		} else if (!has_x) {
			box->x = val;
			has_x = true;
		} else if (!has_y) {
			box->y = val;
			has_y = true;
		} else {
			break;
		}
		pch = strtok(NULL, "x+");
	}

	if (!has_width || !has_height) {
		goto invalid_input;
	}

	free(buf);
	return box;

invalid_input:
	wlr_log(L_ERROR, "could not parse geometry string: %s", str);
	free(buf);
	free(box);
	return NULL;
}

static uint32_t parse_modifier(const char *symname) {
	if (strcmp(symname, "Shift") == 0) {
		return WLR_MODIFIER_SHIFT;
	} else if (strcmp(symname, "Caps") == 0) {
		return WLR_MODIFIER_CAPS;
	} else if (strcmp(symname, "Ctrl") == 0) {
		return WLR_MODIFIER_CTRL;
	} else if (strcmp(symname, "Alt") == 0) {
		return WLR_MODIFIER_ALT;
	} else if (strcmp(symname, "Mod2") == 0) {
		return WLR_MODIFIER_MOD2;
	} else if (strcmp(symname, "Mod3") == 0) {
		return WLR_MODIFIER_MOD3;
	} else if (strcmp(symname, "Logo") == 0) {
		return WLR_MODIFIER_LOGO;
	} else if (strcmp(symname, "Mod5") == 0) {
		return WLR_MODIFIER_MOD5;
	} else {
		return 0;
	}
}

void add_binding_config(struct wl_list *bindings, const char* combination,
		const char* command) {
	struct binding_config *bc = calloc(1, sizeof(struct binding_config));

	xkb_keysym_t keysyms[ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP];
	char *symnames = strdup(combination);
	char* symname = strtok(symnames, "+");
	while (symname) {
		uint32_t modifier = parse_modifier(symname);
		if (modifier != 0) {
			bc->modifiers |= modifier;
		} else {
			xkb_keysym_t sym = xkb_keysym_from_name(symname,
				XKB_KEYSYM_NO_FLAGS);
			if (sym == XKB_KEY_NoSymbol) {
				wlr_log(L_ERROR, "got unknown key binding symbol: %s",
					symname);
				free(bc);
				bc = NULL;
				break;
			}
			keysyms[bc->keysyms_len] = sym;
			bc->keysyms_len++;
		}
		symname = strtok(NULL, "+");
	}
	free(symnames);

	if (bc) {
		wl_list_insert(bindings, &bc->link);
		bc->command = strdup(command);
		bc->keysyms = malloc(bc->keysyms_len * sizeof(xkb_keysym_t));
		memcpy(bc->keysyms, keysyms, bc->keysyms_len * sizeof(xkb_keysym_t));
	}
}

static const char *output_prefix = "output:";
static const char *device_prefix = "device:";

static int config_ini_handler(void *user, const char *section, const char *name,
		const char *value) {
	struct roots_config *config = user;
	if (strcmp(section, "core") == 0) {
		   if (strcmp(name, "xwayland") == 0) {
			   if (strcasecmp(value, "true") == 0) {
					config->xwayland = true;
			   } else if (strcasecmp(value, "false") == 0) {
					config->xwayland = false;
			   } else {
					wlr_log(L_ERROR, "got unknown xwayland value: %s", value);
			   }
		   } else {
			   wlr_log(L_ERROR, "got unknown core config: %s", name);
		   }
	} else if (strncmp(output_prefix, section, strlen(output_prefix)) == 0) {
		const char *output_name = section + strlen(output_prefix);
		struct output_config *oc;
		bool found = false;

		wl_list_for_each(oc, &config->outputs, link) {
			if (strcmp(oc->name, output_name) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			oc = calloc(1, sizeof(struct output_config));
			oc->name = strdup(output_name);
			oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
			oc->scale = 1;
			wl_list_insert(&config->outputs, &oc->link);
		}

		if (strcmp(name, "x") == 0) {
			oc->x = strtol(value, NULL, 10);
		} else if (strcmp(name, "y") == 0) {
			oc->y = strtol(value, NULL, 10);
		} else if (strcmp(name, "scale") == 0) {
			oc->scale = strtol(value, NULL, 10);
			assert(oc->scale >= 1);
		} else if (strcmp(name, "rotate") == 0) {
			if (strcmp(value, "90") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_90;
			} else if (strcmp(value, "180") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_180;
			} else if (strcmp(value, "270") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_270;
			} else if (strcmp(value, "flipped") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED;
			} else if (strcmp(value, "flipped-90") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
			} else if (strcmp(value, "flipped-180") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_180;
			} else if (strcmp(value, "flipped-270") == 0) {
				oc->transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
			} else {
				wlr_log(L_ERROR, "got unknown transform value: %s", value);
			}
		}
	} else if (strcmp(section, "cursor") == 0) {
		if (strcmp(name, "map-to-output") == 0) {
			free(config->cursor.mapped_output);
			config->cursor.mapped_output = strdup(value);
		} else if (strcmp(name, "geometry") == 0) {
			free(config->cursor.mapped_box);
			config->cursor.mapped_box = parse_geometry(value);
		} else {
			wlr_log(L_ERROR, "got unknown cursor config: %s", name);
		}
	} else if (strncmp(device_prefix, section, strlen(device_prefix)) == 0) {
		const char *device_name = section + strlen(device_prefix);
		struct device_config *dc;
		bool found = false;

		wl_list_for_each(dc, &config->devices, link) {
			if (strcmp(dc->name, device_name) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			dc = calloc(1, sizeof(struct device_config));
			dc->name = strdup(device_name);
			wl_list_insert(&config->devices, &dc->link);
		}

		if (strcmp(name, "map-to-output") == 0) {
			free(dc->mapped_output);
			dc->mapped_output = strdup(value);
		} else if (strcmp(name, "geometry") == 0) {
			free(dc->mapped_box);
			dc->mapped_box = parse_geometry(value);
		} else {
			wlr_log(L_ERROR, "got unknown device config: %s", name);
		}
	} else if (strcmp(section, "keyboard") == 0) {
		if (strcmp(name, "meta-key") == 0) {
			config->keyboard.meta_key = parse_modifier(value);
			if (config->keyboard.meta_key == 0) {
				wlr_log(L_ERROR, "got unknown meta key: %s", name);
			}
		} else {
			wlr_log(L_ERROR, "got unknown keyboard config: %s", name);
		}
	} else if (strcmp(section, "bindings") == 0) {
		add_binding_config(&config->bindings, name, value);
	} else {
		wlr_log(L_ERROR, "got unknown config section: %s", section);
	}

	return 1;
}

struct roots_config *parse_args(int argc, char *argv[]) {
	struct roots_config *config = calloc(1, sizeof(struct roots_config));
	if (config == NULL) {
		return NULL;
	}

	config->xwayland = true;
	wl_list_init(&config->outputs);
	wl_list_init(&config->devices);
	wl_list_init(&config->bindings);

	int c;
	while ((c = getopt(argc, argv, "C:E:h")) != -1) {
		switch (c) {
		case 'C':
			config->config_path = strdup(optarg);
			break;
		case 'E':
			config->startup_cmd = strdup(optarg);
			break;
		case 'h':
		case '?':
			usage(argv[0], c != 'h');
		}
	}

	if (!config->config_path) {
		// get the config path from the current directory
		char cwd[MAXPATHLEN];
		if (getcwd(cwd, sizeof(cwd)) != NULL) {
			char buf[MAXPATHLEN];
			snprintf(buf, MAXPATHLEN, "%s/%s", cwd, "rootston.ini");
			config->config_path = strdup(buf);
		} else {
			wlr_log(L_ERROR, "could not get cwd");
			exit(1);
		}
	}

	int result = ini_parse(config->config_path, config_ini_handler, config);

	if (result == -1) {
		wlr_log(L_DEBUG, "No config file found. Using sensible defaults.");
		config->keyboard.meta_key = WLR_MODIFIER_LOGO;
		add_binding_config(&config->bindings, "Logo+Shift+E", "exit");
		add_binding_config(&config->bindings, "Ctrl+q", "close");
		add_binding_config(&config->bindings, "Alt+Tab", "next_window");
	} else if (result == -2) {
		wlr_log(L_ERROR, "Could not allocate memory to parse config file");
		exit(1);
	} else if (result != 0) {
		wlr_log(L_ERROR, "Could not parse config file");
		exit(1);
	}

	return config;
}

void roots_config_destroy(struct roots_config *config) {
	struct output_config *oc, *otmp = NULL;
	wl_list_for_each_safe(oc, otmp, &config->outputs, link) {
		free(oc->name);
		free(oc);
	}

	struct device_config *dc, *dtmp = NULL;
	wl_list_for_each_safe(dc, dtmp, &config->devices, link) {
		free(dc->name);
		free(dc->mapped_output);
		free(dc->mapped_box);
		free(dc);
	}

	struct binding_config *bc, *btmp = NULL;
	wl_list_for_each_safe(bc, btmp, &config->bindings, link) {
		free(bc->keysyms);
		free(bc->command);
		free(bc);
	}

	free(config->config_path);
	free(config->cursor.mapped_output);
	free(config->cursor.mapped_box);
	free(config);
}

struct output_config *config_get_output(struct roots_config *config,
		struct wlr_output *output) {
	struct output_config *o_config;
	wl_list_for_each(o_config, &config->outputs, link) {
		if (strcmp(o_config->name, output->name) == 0) {
			return o_config;
		}
	}

	return NULL;
}

struct device_config *config_get_device(struct roots_config *config,
		struct wlr_input_device *device) {
	struct device_config *d_config;
	wl_list_for_each(d_config, &config->devices, link) {
		if (strcmp(d_config->name, device->name) == 0) {
			return d_config;
		}
	}

	return NULL;
}

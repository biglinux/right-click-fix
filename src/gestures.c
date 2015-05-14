/*

 Copyright 2005 Nir Tzachar
 Copyright 2013 Lucas Augusto Deters


 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 */

#if HAVE_CONFIG_H          
#include <config.h>
#endif

#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <stdio.h>

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include "gestures.h"
#include <regex.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libgen.h>
#include <sys/stat.h>
#include <assert.h>

struct movement** movement_list;
int movement_count;

struct context** context_list;
int context_count;

char * _filename = NULL;

/* alloc a gesture struct */
struct gesture *alloc_gesture(char * gesture_name,
		struct movement *gesture_movement, struct action **gesture_actions,
		int actions_count) {

	assert(gesture_name);
	assert(gesture_movement);
	assert(gesture_actions);
	assert(actions_count >= 0);

	struct gesture *ans = malloc(sizeof(struct gesture));
	bzero(ans, sizeof(struct gesture));

	ans->name = gesture_name;
	ans->movement = gesture_movement;
	ans->actions = gesture_actions;
	ans->actions_count = actions_count;
	return ans;
}

/* release a movement struct */
void free_movement(struct movement *free_me) {

	assert(free_me);

	free(free_me->name);
	free(free_me->expression);
	regfree(free_me->compiled);
	free(free_me->compiled);
	free(free_me);
	return;
}

/* release an action struct */
void free_action(struct action *free_me) {

	assert(free_me);

	free(free_me->data);
	free(free_me);
	return;
}

/* release a gesture struct */
void free_gesture(struct gesture *free_me) {

	assert(free_me);

	free(free_me->name);

	//free_movement(free_me->movement);

	int i = 0;

	for (; i < free_me->actions_count; ++i) {
		free_action(free_me->actions[i]);
	}

	free(free_me);

	return;
}

void free_context(struct context * free_me) {
	assert(free_me);

	int g = 0;

	for (; g < free_me->gestures_count; ++g) {
		free_gesture(free_me->gestures[g]);
	}

	free(free_me->name);
	free(free_me->title);
	free(free_me->class);
	regfree(free_me->class_compiled);
	free(free_me->class_compiled);
	regfree(free_me->title_compiled);
	free(free_me->title_compiled);

	int i = 0;
	for (; i < free_me->gestures_count; ++i) {
		free_gesture(free_me->gestures[i]);
	}
	free(free_me);

}

void gestures_finalize() {

	free(_filename);

	int i;

	for (i = 0; i < context_count; ++i) {
		free_context(context_list[i]);
	}

	for (i = 0; i < movement_count; ++i) {
		free_movement(movement_list[i]);
	}

	free(context_list);
	free(movement_list);

	return;
}

/* alloc a window struct */
struct context *alloc_context(char * context_name, char *window_title,
		char *window_class, struct gesture ** gestures, int gestures_count,
		int abort) {
	assert(context_name);
	assert(window_title);
	assert(window_class);
	assert(gestures);
	assert(gestures_count >= 0);

	struct context *ans = malloc(sizeof(struct context));
	bzero(ans, sizeof(struct context));

	ans->name = context_name;
	ans->title = window_title;
	ans->class = window_class;
	ans->gestures = gestures;
	ans->gestures_count = gestures_count;

	// TODO: GET ERROR DETAILS WITH regerror

	regex_t * title_compiled = NULL;
	if (ans->title) {

		title_compiled = malloc(sizeof(regex_t));
		if (regcomp(title_compiled, window_title,
		REG_EXTENDED | REG_NOSUB)) {
			fprintf(stderr, "Error compiling regexp: %s\n", window_title);
			regfree(title_compiled);
			title_compiled = NULL;
		}
	}
	ans->title_compiled = title_compiled;

	regex_t * class_compiled = NULL;
	if (ans->class) {
		class_compiled = malloc(sizeof(regex_t));
		if (regcomp(class_compiled, window_class,
		REG_EXTENDED | REG_NOSUB)) {
			fprintf(stderr, "Error compiling regexp: %s\n", window_class);
			regfree(class_compiled);
			class_compiled = NULL;
		}
	}
	ans->class_compiled = class_compiled;

	return ans;
}

/* alloc a movement struct */
struct movement *alloc_movement(char *movement_name, char *movement_expression) {

	assert(movement_name);
	assert(movement_expression);

	struct movement *ans = malloc(sizeof(struct movement));
	bzero(ans, sizeof(struct movement));

	ans->name = movement_name;
	ans->expression = movement_expression;

	char * regex_str = NULL;
	asprintf(&regex_str,"^(%s)$",movement_expression);

	regex_t * movement_compiled = NULL;
	movement_compiled = malloc(sizeof(regex_t));
	if (regcomp(movement_compiled, regex_str,
	REG_EXTENDED | REG_NOSUB) != 0) {
		fprintf(stderr, "Warning: Invalid movement sequence: %s\n", regex_str);
	}
	free(regex_str);

	ans->compiled = movement_compiled;

	return ans;
}

/* alloc an action struct */
struct action *alloc_action(int action_type, char * action_value) {

	assert(action_type >= 0);

	struct action * ans = malloc(sizeof(struct action));

	bzero(ans, sizeof(struct action));

	ans->type = action_type;
	ans->data = action_value;

	return ans;
}

struct gesture * gesture_match(char * captured_sequence, char * window_class,
		char * window_title) {

	assert(captured_sequence);
	assert(window_class);
	assert(window_title);

	struct gesture * matched_gesture = NULL;

	int c = 0;

	for (c = 0; c < context_count; ++c) {

		struct context * context = context_list[c];

		if ((!context->class)
				|| (regexec(context->class_compiled, window_class, 0,
						(regmatch_t *) NULL, 0) != 0)) {
			continue;
		}

		if ((!context->title)
				|| (regexec(context->title_compiled, window_title, 0,
						(regmatch_t *) NULL, 0)) != 0) {
			continue;
		}

		if (context->gestures_count) {

			int g = 0;

			for (g = 0; g < context->gestures_count; ++g) {

				struct gesture * gest = context->gestures[g];

				if (gest->movement) {

					if ((gest->movement->compiled)
							&& (regexec(gest->movement->compiled,
									captured_sequence, 0, (regmatch_t *) NULL,
									0) == 0)) {

						matched_gesture = gest;
						break;

					}

				}

			}

		}

	}

	return matched_gesture;
}

static void recursive_mkdir(char *path, mode_t mode) {

	assert(path);

	char *spath = strdup(path);
	char *next_dir = dirname(spath);

	if (access(next_dir, F_OK) == 0) {
		goto done;
	}

	if (strcmp(next_dir, ".") == 0 || strcmp(next_dir, "/") == 0) {
		goto done;
	}

	recursive_mkdir(next_dir, mode);
	mkdir(next_dir, mode);

	done: free(spath);
	return;
}

/**
 * Copy a file
 */
int file_create_from_template(char *tofile, char *fromfile) {

	assert(tofile);
	assert(fromfile);

	recursive_mkdir(tofile, S_IRWXU | S_IRGRP);

	FILE *in, *out;
	char ch;

	if ((in = fopen(fromfile, "rb")) == NULL) {
		fprintf(stderr, "Cannot open input file: %s\n", fromfile);
		return -1;
	}
	if ((out = fopen(tofile, "wb")) == NULL) {
		fprintf(stderr, "Cannot open output file: %s\n", tofile);
		return -2;
	}

	while (!feof(in)) {
		ch = getc(in);
		if (ferror(in)) {
			printf("Read Error\n");
			clearerr(in);
			break;
		} else {
			if (!feof(in))
				putc(ch, out);
			if (ferror(out)) {
				fprintf(stderr, "Write Error\n");
				clearerr(out);
				break;
			}
		}
	}
	fclose(in);
	fclose(out);

	return 0;

}

struct action * parse_action(xmlNode *node) {

	assert(node);

	char * action_name = NULL;
	char * action_value = NULL;

	xmlAttr* attribute = node->properties;
	while (attribute && attribute->name && attribute->children) {

		char * name = (char *) attribute->name;
		char * value = (char *) xmlNodeListGetString(node->doc,
				attribute->children, 1);

		if (strcasecmp(name, "action") == 0) {
			action_name = strdup(value);
		} else if (strcasecmp(name, "value") == 0) {
			action_value = strdup(value);
		}

		xmlFree(value);
		attribute = attribute->next;
	}

	struct action * a = NULL;

	if (!action_name) {
		fprintf(stderr, "Missing action name\n");

		free(action_value);

	} else {

		if (!action_value) {
			action_value = "";
		}

		int id = ACTION_ERROR;

		if (strcasecmp(action_name, "iconify") == 0) {
			id = ACTION_ICONIFY;
		} else if (strcasecmp(action_name, "kill") == 0) {
			id = ACTION_KILL;
		} else if (strcasecmp(action_name, "lower") == 0) {
			id = ACTION_LOWER;
		} else if (strcasecmp(action_name, "raise") == 0) {
			id = ACTION_RAISE;
		} else if (strcasecmp(action_name, "maximize") == 0) {
			id = ACTION_MAXIMIZE;
		} else if (strcasecmp(action_name, "keypress") == 0) {
			id = ACTION_ROOT_SEND;
		} else if (strcasecmp(action_name, "exec") == 0) {
			id = ACTION_EXECUTE;
		} else {
			fprintf(stderr, "unknown action: %s\n", action_name);
		}

		a = alloc_action(id, action_value);

		free(action_name);

	}

	return a;

}

struct movement * movement_find(char * movement_name,
		struct movement ** known_movements, int known_movements_count) {

	if (!movement_name) {
		return NULL;
	}

	int i = 0;

	for (i = 0; i < known_movements_count; ++i) {
		struct movement * m = known_movements[i];

		if ((m->name) && (movement_name)
				&& (strcasecmp(movement_name, m->name) == 0)) {
			return m;
		}
	}

	return NULL;

}

struct gesture * parse_gesture(xmlNode *node,
		struct movement ** known_movements, int known_movements_count) {

	assert(node);
	assert(known_movements);

	char * gesture_name = NULL;
	struct movement * gesture_trigger = NULL;

	xmlAttr* attribute = node->properties;
	while (attribute && attribute->name && attribute->children) {

		char * name = (char *) attribute->name;
		char * value = (char *) xmlNodeListGetString(node->doc,
				attribute->children, 1);

		if (strcasecmp(name, "name") == 0) {
			gesture_name = strdup(value);
		} else if (strcasecmp(name, "movement") == 0) {
			gesture_trigger = movement_find(value, known_movements,
					known_movements_count);
		}
		xmlFree(value);
		attribute = attribute->next;
	}

	if (!gesture_name) {
		//free(gesture_trigger);
		fprintf(stderr, "Missing gesture name\n");
		return NULL;
	}

	if (!gesture_trigger) {
		free(gesture_name);
		fprintf(stderr, "Missing gesture movement.\n");
		return NULL;
	}

	struct action ** gesture_actions = malloc(sizeof(struct action *) * 254);
	int actions_count = 0;

	xmlNode *cur_node = NULL;

	for (cur_node = node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {

			char * element = (char *) cur_node->name;

			if (strcasecmp(element, "do") == 0) {

				struct action * a = parse_action(cur_node);

				if (a) {
					gesture_actions[actions_count++] = a;
				}

			}

		}
	}

	if (!actions_count) {
		fprintf(stderr,
				"Alert: No actions associated to the gesture %s. Ignoring.\n",
				gesture_name);
	}

	struct gesture * gest = NULL;

	gest = alloc_gesture(gesture_name, gesture_trigger, gesture_actions,
			actions_count);

	return gest;

}

struct context * parse_context(xmlNode *node,
		struct movement ** known_movements, int known_movements_count) {

	assert(node);
	assert(known_movements);
	assert(known_movements_count >= 0);

	// parse node attributes (context name, window_title, window_class)

	char * context_name = NULL;
	char * window_title = NULL;
	char * window_class = NULL;

	xmlAttr* attribute = node->properties;
	while (attribute && attribute->name && attribute->children) {
		char * name = (char *) attribute->name;
		char * value = (char *) xmlNodeListGetString(node->doc,
				attribute->children, 1);

		if (strcasecmp(name, "name") == 0) {
			context_name = strdup(value);
		} else if (strcasecmp(name, "windowtitle") == 0) {
			window_title = strdup(value);
		} else if (strcasecmp(name, "windowclass") == 0) {
			window_class = strdup(value);
		}
		xmlFree(value);
		attribute = attribute->next;
	}

	if (!context_name) {

		free(window_title);
		free(window_class);
		fprintf(stderr, "Missing context name\n");
		return NULL;

	}

	if (!window_class) {
		window_class = "";
	}

	if (!window_title) {
		window_title = "";
	}

	// parse node items (context gestures)

	xmlNode *cur_node = NULL;

	struct gesture ** gestures = malloc(sizeof(struct gesture *) * 254);
	struct gesture * gest = NULL;
	int gestures_count = 0;
	int abort = 0;

	for (cur_node = node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {

			char * element = (char *) cur_node->name;

			if (strcasecmp(element, "gesture") == 0) {

				gest = parse_gesture(cur_node, known_movements,
						known_movements_count);
				if (gest) {
					gestures[gestures_count++] = gest;
				}

			} else if (strcasecmp(element, "abort") == 0) {
				abort = 1;
			}
		}

	}

	struct context * ctx = alloc_context(context_name, window_title,
			window_class, gestures, gestures_count, abort);

	return ctx;

}

struct movement * parse_movement(xmlNode *node) {

	xmlNode *cur_node = NULL;

	char * movement_name = NULL;
	char * movement_strokes = NULL;

	struct movement * ans = NULL;

	xmlAttr* attribute = node->properties;
	while (attribute && attribute->name && attribute->children) {

		char * name = (char *) attribute->name;
		char * value = (char *) xmlNodeListGetString(node->doc,
				attribute->children, 1);

		if (strcasecmp(name, "name") == 0) {
			movement_name = strdup(value);
		} else if (strcasecmp(name, "value") == 0) {
			movement_strokes = strdup(value);
		}
		xmlFree(value);
		attribute = attribute->next;
	}

	if (movement_name && movement_strokes) {
		ans = alloc_movement(movement_name, movement_strokes);
	}

	return ans;

}

int parse_root(xmlNode *node) {

	assert(node);

	xmlNode *cur_node = NULL;

	struct movement ** new_movement_list = malloc(
			sizeof(struct movement *) * 254);
	int new_movement_count = 0;

	struct context ** new_context_list = malloc(sizeof(struct context *) * 254);
	int new_context_count = 0;

	int gestures_count = 0;

	for (cur_node = node->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {

			char * element = (char *) cur_node->name;

			if (strcasecmp(element, "movement") == 0) {

				struct movement * m = parse_movement(cur_node);
				if (m) {
					new_movement_list[new_movement_count++] = m;
				}

				// found a "context" token
			} else if (strcasecmp(element, "context") == 0) {

				struct context * ctx = parse_context(cur_node,
						new_movement_list, new_movement_count);
				if (ctx) {
					new_context_list[new_context_count++] = ctx;
					gestures_count += ctx->gestures_count;

				}

			} else {
				fprintf(stderr,
						"Expecting only 'movement' or 'context' at root. Ignoring\n");
			}

		}

	}

	fprintf(stdout, "Loaded %i movements.\n", new_movement_count);
	fprintf(stdout, "Loaded %i contexts with %i gestures.\n", new_context_count,
			gestures_count);

	// update global variables

	movement_list = new_movement_list;
	movement_count = new_movement_count;

	context_list = new_context_list;
	context_count = new_context_count;

	return 0;

}

/**
 * Reads the conf file
 */
int gestures_load_from_file(char *filename) {

	assert(filename);

	int result = 0;

	xmlDocPtr doc = NULL;
	xmlNode *root_element = NULL;

	doc = xmlParseFile(filename);

	if (!doc) {
		return 1;
	}

	root_element = xmlDocGetRootElement(doc);
	result = parse_root(root_element);

	xmlFreeDoc(doc);
	xmlCleanupParser();

	return result;

}

char * gestures_get_template_filename() {
	char * template_file = NULL;
	asprintf(&template_file, "%s/mygestures.xml", SYSCONFIR);
	return template_file;
}

int gestures_init() {

	if (!_filename) {

		char * xdg = NULL;

		// dont need to be freed
		xdg = getenv("XDG_CONFIG_HOME");

		if (xdg) {
			asprintf(&_filename, "%s/mygestures/mygestures.xml", xdg);
		} else {
			char * home = getenv("HOME");
			asprintf(&_filename, "%s/.config/mygestures/mygestures.xml", home);
		}

	}

	fprintf(stdout, "Loading configuration from %s\n", _filename);

	int err = gestures_load_from_file(_filename);

	if (err) {

		FILE *f = NULL;
		f = fopen(_filename, "r");

		if (f) {
			fclose(f);
		} else {

			char * template_filename = gestures_get_template_filename();

			err = file_create_from_template(_filename, template_filename);

			if (err) {
				fprintf(stderr,
						"Error trying to create config file `%s' from template `%s'.\n",
						_filename, template_filename);
			} else {
				fprintf(stderr,
						"Created config file `%s' from template `%s'.\n",
						_filename, template_filename);
			}

			err = gestures_load_from_file(_filename);

			free(template_filename);

		}

	}

	return err;

}

// --------------------------------------------------------------------------------------------
//                                        PUBLIC
// --------------------------------------------------------------------------------------------

void gestures_set_config_file(char * config_file) {

	_filename = config_file;

}


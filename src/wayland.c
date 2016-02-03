/*
 * Copyright 2016 Jonathan Eyolfson
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/memfd.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include "xdg_shell.h"

#include "exit_code.h"

static struct wl_compositor *wl_compositor = NULL;
static struct wl_shm *wl_shm = NULL;
static struct xdg_shell *xdg_shell = NULL;
static struct xdg_surface *xdg_surface = NULL;

static int32_t current_width;
static int32_t current_height;

struct buffer {
	int32_t fd;
	uint32_t *data;
	int32_t width;
	int32_t stride;
	int32_t height;
	int32_t capacity;
	cairo_surface_t *cairo_surface;
	cairo_t *cairo;
	struct wl_shm_pool *wl_shm_pool;
	struct wl_buffer *wl_buffer;
};

struct buffer buffers[2] = {{0}, {0}};
struct buffer *back_buffer = &buffers[0];

void swap_buffers()
{
	if (back_buffer == &buffers[0]) {
		back_buffer = &buffers[1];
	}
	else {
		back_buffer = &buffers[0];
	}
}

void init_buffer(struct buffer *buffer)
{
	buffer->fd = syscall(
		SYS_memfd_create, "irc-client", MFD_CLOEXEC | MFD_ALLOW_SEALING
	);
	if (buffer->fd < 0) {
		set_exit_code(1);
		return;
	}
	buffer->width = current_width;
	buffer->stride = buffer->width * sizeof(uint32_t);
	buffer->height = current_height;
	buffer->capacity = buffer->stride * buffer->height;
	if (ftruncate(buffer->fd, buffer->capacity) < 0) {
		set_exit_code(1);
		close(buffer->fd);
		return;
	}
	buffer->data = mmap(
		NULL, buffer->capacity, PROT_WRITE | PROT_READ, MAP_SHARED,
		buffer->fd, 0
	);
	if (buffer->data == MAP_FAILED) {
		set_exit_code(1);
		close(buffer->fd);
		return;
	}
	buffer->cairo_surface = cairo_image_surface_create_for_data(
		(unsigned char *) buffer->data, CAIRO_FORMAT_ARGB32,
		buffer->width, buffer->height, buffer->stride
	);
	buffer->cairo = cairo_create(buffer->cairo_surface);

	buffer->wl_shm_pool = wl_shm_create_pool(wl_shm, buffer->fd, buffer->capacity);
	buffer->wl_buffer = wl_shm_pool_create_buffer(
		buffer->wl_shm_pool, 0, buffer->width, buffer->height,
		buffer->stride, WL_SHM_FORMAT_ARGB8888
	);
}

void init_buffers()
{
	if (current_width == 0 || current_height == 0) {
		set_exit_code(1);
		return;
	}

	init_buffer(&buffers[0]);
	if (is_exiting()) {
		return;
	}

	init_buffer(&buffers[1]);
}

void fini_buffer(struct buffer *buffer)
{
	cairo_destroy(buffer->cairo);
	cairo_surface_destroy(buffer->cairo_surface);
	wl_buffer_destroy(buffer->wl_buffer);
	wl_shm_pool_destroy(buffer->wl_shm_pool);
	munmap(buffer->data, buffer->capacity);
	close(buffer->fd);
}

void fini_buffers()
{
	fini_buffer(&buffers[0]);
	fini_buffer(&buffers[1]);
}

static void global(void *data,
                   struct wl_registry *wl_registry,
                   uint32_t name,
                   const char *interface,
                   uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		wl_compositor = wl_registry_bind(
			wl_registry,
			name,
			&wl_compositor_interface,
			version);
	}
	else if (strcmp(interface, wl_shm_interface.name) == 0) {
		wl_shm = wl_registry_bind(
			wl_registry,
			name,
			&wl_shm_interface,
			version);
	}
	else if (strcmp(interface, xdg_shell_interface.name) == 0) {
		xdg_shell = wl_registry_bind(
			wl_registry,
			name,
			&xdg_shell_interface,
			version);
	}
}

static void global_remove(void *data,
                          struct wl_registry *wl_registry,
                          uint32_t name)
{
}

static void ping(void *data, struct xdg_shell *xdg_shell, uint32_t serial)
{
	xdg_shell_pong(xdg_shell, serial);
}

static void xdg_surface_configure(
	void *data, struct xdg_surface *xdg_surface, int32_t width,
	int32_t height, struct wl_array *states, uint32_t serial)
{
	current_width = width;
	current_height = height;
	xdg_surface_ack_configure(xdg_surface, serial);
}

static void xdg_surface_close(void *data, struct xdg_surface *xdg_surface)
{
}

static void draw(cairo_t *cr)
{
	struct timespec timespec;
	struct tm tm;
	char buffer[200];
	clock_gettime(CLOCK_REALTIME, &timespec);
	localtime_r(&timespec.tv_sec, &tm);
	sprintf(buffer, "%02d:%02d:%02d.%03ld",
		tm.tm_hour, tm.tm_min, tm.tm_sec, timespec.tv_nsec / 1000000);

	cairo_set_source_rgb(cr, 0, 0.169, 0.212);
	cairo_paint(cr);
	cairo_set_line_width(cr, 1);
	cairo_set_source_rgb(cr, 0.345, 0.431, 0.459);
	cairo_rectangle(cr, 10, 10, 280, 180);
	cairo_stroke(cr);

	cairo_arc(cr, 330, 60, 40, 0, 2*M_PI);
	cairo_close_path(cr);
	cairo_stroke(cr);

	cairo_select_font_face(cr, "Cousine",
		CAIRO_FONT_SLANT_NORMAL,
		CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 13);

	cairo_set_source_rgb(cr, 0.514, 0.580, 0.589);
	cairo_move_to(cr, 20, 30);
	cairo_show_text(cr, "Connecting..");

	cairo_set_source_rgb(cr, 0.149, 0.545, 0.824);
	cairo_move_to(cr, 20, 50);
	cairo_show_text(cr, buffer);
	cairo_identity_matrix(cr);
}

static struct wl_registry_listener registry_listener = {global, global_remove};
static struct xdg_shell_listener xdg_shell_listener = {ping};
static struct xdg_surface_listener xdg_surface_listener = {
	xdg_surface_configure,
	xdg_surface_close
};

void *wayland_start(void *arg)
{
	struct wl_display *wl_display = wl_display_connect(NULL);
	if (wl_display == NULL) {
		printf("wl_display failed\n");
		set_exit_code(2);
		return NULL;
	}
	struct wl_registry *wl_registry = wl_display_get_registry(wl_display);
	if (wl_registry == NULL) {
		printf("wl_registry failed\n");
	}

	wl_registry_add_listener(wl_registry, &registry_listener, NULL);
	wl_display_dispatch(wl_display);
	if (wl_compositor == NULL) {
		printf("wl_compositor failed\n");
	}
	if (wl_shm == NULL) {
		printf("wl_shm failed\n");
	}
	if (xdg_shell == NULL) {
		printf("xdg_shell failed\n");
	}
	xdg_shell_add_listener(xdg_shell, &xdg_shell_listener, NULL);
	xdg_shell_use_unstable_version(xdg_shell, XDG_SHELL_VERSION_CURRENT);

	struct wl_surface *wl_surface = wl_compositor_create_surface(wl_compositor);
	if (wl_surface == NULL) {
		printf("wl_surface failed\n");
	}
	xdg_surface = xdg_shell_get_xdg_surface(xdg_shell, wl_surface);
	if (xdg_surface == NULL) {
		printf("xdg_surface failed\n");
	}
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_surface_set_title(xdg_surface, "IRC Client");
	xdg_surface_set_maximized(xdg_surface);
	wl_display_roundtrip(wl_display); /* Get width and height */

	init_buffers();
	if (is_exiting()) {
		return NULL;
	}

	while (1) {
		draw(back_buffer->cairo);

		wl_surface_attach(wl_surface, back_buffer->wl_buffer, 0, 0);
		wl_surface_commit(wl_surface);
		wl_surface_damage(wl_surface, 0, 0, back_buffer->width, back_buffer->height);
		xdg_surface_set_window_geometry(xdg_surface,
			0, 0, back_buffer->width, back_buffer->height);

		swap_buffers();

		wl_display_roundtrip(wl_display);
		if (is_exiting()) {
			break;
		}
		/* TODO: resize back buffer */
	}

	fini_buffers();

	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(wl_surface);

	xdg_shell_destroy(xdg_shell);
	wl_shm_destroy(wl_shm);
	wl_compositor_destroy(wl_compositor);
	wl_registry_destroy(wl_registry);
	wl_display_disconnect(wl_display);
	return NULL;
}

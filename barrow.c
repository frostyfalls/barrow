#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

struct barrow_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	struct wlr_output_layout *output_layout;
	struct wlr_cursor *cursor;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_seat *seat;
	struct wl_listener new_output;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list outputs; // barrow_output::link
	struct wl_list toplevels; // barrow_toplevel::link
};

struct barrow_toplevel {
	struct wl_list link;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
};

struct barrow_output {
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_list link;
};

struct barrow_server server;

void output_frame(struct wl_listener *listener, void *data) {
	struct barrow_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = server.scene;

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);

	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

void server_new_output(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server.allocator, server.renderer);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}

	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	struct barrow_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	wl_list_insert(&server.outputs, &output->link);

	struct wlr_output_layout_output *layout_output = wlr_output_layout_add_auto(server.output_layout, wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server.scene, wlr_output);
	wlr_scene_output_layout_add_output(server.scene_layout, layout_output, scene_output);
}

void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	struct barrow_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	wl_list_insert(&server.toplevels, &toplevel->link);
}

void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	struct barrow_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	wl_list_remove(&toplevel->link);
}

void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	struct barrow_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}
}

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct barrow_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->scene_tree = wlr_scene_xdg_surface_create(&server.scene->tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;

	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);
}

void init() {
	server.display = wl_display_create();
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.display), NULL);
	server.renderer = wlr_renderer_autocreate(server.backend);
	wlr_renderer_init_wl_shm(server.renderer, server.display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	server.scene = wlr_scene_create();
	server.output_layout = wlr_output_layout_create(server.display);
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	server.compositor = wlr_compositor_create(server.display, 6, server.renderer);
	wlr_subcompositor_create(server.display);
	wlr_data_device_manager_create(server.display);

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	server.seat = wlr_seat_create(server.display, "seat0");

	wl_list_init(&server.toplevels);
	server.xdg_shell = wlr_xdg_shell_create(server.display, 3);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);

	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);
}

void run() {
	const char *socket;
	pid_t child_pid;
	int rw[2];
	char *startup_cmd = "foot";

	socket = wl_display_add_socket_auto(server.display);
	setenv("WAYLAND_DISPLAY", socket, true);

	if (!wlr_backend_start(server.backend)) {
		abort();
	}

	if (pipe(rw) == -1) {
		abort();
	}
	if ((child_pid = fork()) == -1) {
		abort();
	}
	if (child_pid == 0) {
		setsid();
		dup2(rw[0], STDIN_FILENO);
		close(rw[0]);
		close(rw[1]);
		execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
		abort();
	}
	dup2(rw[1], STDOUT_FILENO);
	close(rw[1]);
	close(rw[0]);

	wl_display_run(server.display);
}

void clean() {
	wl_display_destroy(server.display);
}

int main() {
	init();
	run();
	clean();

	return EXIT_SUCCESS;
}

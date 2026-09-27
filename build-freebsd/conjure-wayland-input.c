/* conjure-wayland-input.c — give the running sway a virtual keyboard and
 * pointer, so its seat stops reporting zero capabilities.
 *
 * WHY: the Wayland.backend's shm/ARGB8888 format negotiation refuses to
 * proceed when the seat has no capabilities. sway on the WLR_HEADLESS
 * backend comes up with an output but seat0 at "capabilities": 0 and an empty
 * device list, because the headless backend has no input devices to create:
 *
 *     $ swaymsg -t get_seats
 *     [ { "name": "seat0", "capabilities": 0, "focus": 6, "devices": [] } ]
 *
 * That is the state the format negotiation refuses to guess from. The only
 * way to add an input device without root (uinput needs /dev/uinput) is the
 * compositor's own virtual-input protocols: zwlr_virtual_pointer_manager_v1
 * and zwp_virtual_keyboard_manager_v1. So bind both, create a device on
 * seat0 for each, and hold the connection open. Afterwards:
 *
 *     "capabilities": 3        (POINTER=1 | KEYBOARD=2)
 *     devices: wlr_virtual_keyboard_v1 (keyboard), wlr_virtual_pointer_v1 (pointer)
 *
 * THE DEVICES EXIST ONLY WHILE THIS PROCESS RUNS. That is the whole design:
 * the seat gains the capabilities the moment the requests land and loses them
 * when this exits. Run it in the background for the duration of a guest test,
 * or the guest will see the same empty seat it saw before.
 *
 * Two things that cost time to find, so they are written down:
 *   - wl_proxy_marshal only QUEUES a request. Nothing reaches the compositor
 *     until a flush (or a roundtrip/dispatch, which flush first). A loop of
 *     wl_display_dispatch_pending() alone creates the device locally and the
 *     seat stays empty, with no error anywhere -- which is what the first
 *     version did.
 *   - the pointer is moved once and framed, so the device is unambiguously
 *     in use rather than merely allocated.
 *
 * Host tool, not a guest test: an ordinary FreeBSD binary talking Wayland to
 * the host compositor. Build:
 *
 *   cd build-freebsd
 *   wayland-scanner client-header wlr-protocols/wlr-virtual-pointer-unstable-v1.xml \
 *       wlr-virtual-pointer-client-protocol.h
 *   wayland-scanner private-code  wlr-protocols/wlr-virtual-pointer-unstable-v1.xml \
 *       wlr-virtual-pointer-protocol.c
 *   wayland-scanner client-header wlr-protocols/virtual-keyboard-unstable-v1.xml \
 *       virtual-keyboard-client-protocol.h
 *   wayland-scanner private-code  wlr-protocols/virtual-keyboard-unstable-v1.xml \
 *       virtual-keyboard-protocol.c
 *   clang -O1 -Wall -o conjure-wayland-input conjure-wayland-input.c \
 *       wlr-virtual-pointer-protocol.c virtual-keyboard-protocol.c \
 *       $(pkg-config --cflags --libs wayland-client)
 *
 * Run (needs the compositor's env, and its ipc socket for checking):
 *
 *   XDG_RUNTIME_DIR=/tmp/wayland-test WAYLAND_DISPLAY=wayland-1 \
 *     ./conjure-wayland-input 3600 &
 *   SWAYSOCK=/tmp/wayland-test/sway-ipc.<uid>.<pid>.sock swaymsg -t get_seats
 *
 * The two protocol XMLs next to this file are wlroots' own copies
 * (wlroots/protocol/), MIT, and are what wayland-scanner consumes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "wlr-virtual-pointer-client-protocol.h"
#include "virtual-keyboard-client-protocol.h"

static struct zwlr_virtual_pointer_manager_v1 *manager;
static struct zwp_virtual_keyboard_manager_v1 *kbmanager;
static struct zwp_virtual_keyboard_v1 *keyboard;
static struct zwlr_virtual_pointer_v1 *pointer;
static struct wl_seat *seat;
static int have_manager, have_seat, have_kbmanager;

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t ver) {
	(void)data;
	if (strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
		manager = wl_registry_bind(reg, name, &zwlr_virtual_pointer_manager_v1_interface,
		                           ver < 2 ? ver : 2);
		have_manager = 1;
		printf("bound %s v%u (global name %u)\n", iface,
		       wl_proxy_get_version((struct wl_proxy *)manager), name);
	} else if (strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
		kbmanager = wl_registry_bind(reg, name, &zwp_virtual_keyboard_manager_v1_interface,
		                             ver < 1 ? ver : 1);
		have_kbmanager = 1;
		printf("bound %s v%u (global name %u)\n", iface,
		       wl_proxy_get_version((struct wl_proxy *)kbmanager), name);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(reg, name, &wl_seat_interface, ver < 5 ? ver : 5);
		have_seat = 1;
		printf("bound %s v%u (global name %u)\n", iface,
		       wl_proxy_get_version((struct wl_proxy *)seat), name);
	}
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	(void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	registry_global, registry_global_remove
};

int main(int argc, char **argv) {
	struct wl_display *display;
	struct wl_registry *registry;
	int hold_secs = (argc > 1) ? atoi(argv[1]) : 3600;
	int i;

	setvbuf(stdout, NULL, _IONBF, 0);

	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "wl_display_connect failed (WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s)\n",
		        getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)",
		        getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");
		return 2;
	}
	printf("connected (fd=%d)\n", wl_display_get_fd(display));

	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (!have_manager) {
		fprintf(stderr, "compositor does not advertise %s -- nothing to do\n",
		        zwlr_virtual_pointer_manager_v1_interface.name);
		return 3;
	}
	if (!have_seat) {
		fprintf(stderr, "compositor advertises no wl_seat\n");
		return 4;
	}

	/* Roundtrip again: the seat bind may have arrived in the same batch. */
	wl_display_roundtrip(display);

	pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, seat);
	if (pointer == NULL) {
		fprintf(stderr, "create_virtual_pointer returned NULL\n");
		return 5;
	}
	/* The device is only real once the request has actually left this
	 * process. wl_proxy_marshal queues it; nothing sends it until a flush
	 * (or a roundtrip/dispatch, which flush first). Without this the
	 * compositor never sees the request and the seat stays empty --
	 * which is exactly what the first attempt did. */
	if (wl_display_flush(display) < 0) {
		fprintf(stderr, "wl_display_flush failed: %s\n", strerror(errno));
		return 8;
	}
	/* Move it once so the device is unambiguously in use, then flush again. */
	zwlr_virtual_pointer_v1_motion_absolute(pointer, 100, 640, 360, 640, 360);
	zwlr_virtual_pointer_v1_frame(pointer);

	/* Keyboard half. The backend's negotiation wants keyboard>0 as well as
	 * pointer>0, and WL_SEAT_CAPABILITY_POINTER alone leaves it at 1. */
	if (have_kbmanager) {
		keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(kbmanager, seat);
		if (keyboard != NULL) {
			printf("virtual keyboard created on the seat\n");
		} else {
			fprintf(stderr, "create_virtual_keyboard returned NULL\n");
		}
	} else {
		fprintf(stderr, "NOTE: compositor does not advertise %s; pointer only\n",
		        zwp_virtual_keyboard_manager_v1_interface.name);
	}
	if (wl_display_flush(display) < 0) {
		fprintf(stderr, "wl_display_flush (motion) failed\n");
		return 9;
	}
	/* Roundtrip so the reply (if any) is processed and sway has certainly
	 * run its own event loop for the new device. */
	wl_display_roundtrip(display);
	printf("virtual pointer created + flushed; holding it for %ds\n", hold_secs);
	printf("now check the seat with:  SWAYSOCK=... swaymsg -t get_seats\n");

	/* The device lives as long as we do. Nudge the connection so a
	 * compositor-side removal is noticed rather than silently ignored. */
	for (i = 0; i < hold_secs * 10; i++) {
		wl_display_flush(display);
		if (wl_display_dispatch_pending(display) < 0) {
			fprintf(stderr, "connection lost after %d.%ds\n", i / 10, (i % 10) * 100);
			return 6;
		}
		usleep(100000);
		if (wl_display_get_error(display) != 0) {
			fprintf(stderr, "wayland error after %d.%ds\n", i / 10, (i % 10) * 100);
			return 7;
		}
	}
	printf("releasing the virtual pointer\n");
	if (keyboard != NULL) {
		zwp_virtual_keyboard_v1_destroy(keyboard);
	}
	zwlr_virtual_pointer_v1_destroy(pointer);
	wl_display_disconnect(display);
	return 0;
}

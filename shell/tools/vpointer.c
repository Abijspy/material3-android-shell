/*
 * vpointer: drive a virtual pointer (wlr-virtual-pointer-unstable-v1) for
 * headless UI tests. Commands run in order:
 *   vpointer move X Y | click [left|right|middle] | down [btn] | up [btn]
 *            | scroll N | sleep MS ...
 * Coordinates are output pixels; VPOINTER_EXTENT=WxH sets the output size
 * (default 1600x900).
 */
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct zwlr_virtual_pointer_manager_v1* manager;
static struct wl_seat* seat;

static void global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t v) {
    (void)d;
    if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
        manager = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(r, name, &wl_seat_interface, v < 1 ? v : 1);
}
static void global_remove(void* d, struct wl_registry* r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener listener = {global, global_remove};

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static uint32_t button(const char* s) {
    if (!s || !strcmp(s, "left")) return BTN_LEFT;
    if (!strcmp(s, "right")) return BTN_RIGHT;
    if (!strcmp(s, "middle")) return BTN_MIDDLE;
    return BTN_LEFT;
}

int main(int argc, char** argv) {
    struct wl_display* display = wl_display_connect(NULL);
    if (!display) return fprintf(stderr, "vpointer: no Wayland display\n"), 1;
    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, NULL);
    wl_display_roundtrip(display);
    if (!manager) return fprintf(stderr, "vpointer: compositor lacks wlr-virtual-pointer\n"), 1;
    struct zwlr_virtual_pointer_v1* p = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, seat);

    unsigned w = 1600, h = 900;
    const char* ext = getenv("VPOINTER_EXTENT");
    if (ext) sscanf(ext, "%ux%u", &w, &h);

    for (int i = 1; i < argc; ++i) {
        const char* cmd = argv[i];
        if (!strcmp(cmd, "move") && i + 2 < argc) {
            const unsigned x = (unsigned)atoi(argv[i + 1]), y = (unsigned)atoi(argv[i + 2]);
            i += 2;
            zwlr_virtual_pointer_v1_motion_absolute(p, now_ms(), x, y, w, h);
            zwlr_virtual_pointer_v1_frame(p);
            /* Sway drops a new device's first press if it follows the first
             * motion immediately (pointer focus is set up in between) */
            wl_display_roundtrip(display);
            usleep(150000);
        } else if (!strcmp(cmd, "click") || !strcmp(cmd, "down") || !strcmp(cmd, "up")) {
            const char* b = i + 1 < argc && strchr("lrm", argv[i + 1][0]) ? argv[++i] : NULL;
            const uint32_t code = button(b);
            if (strcmp(cmd, "up")) {
                zwlr_virtual_pointer_v1_button(p, now_ms(), code, WL_POINTER_BUTTON_STATE_PRESSED);
                zwlr_virtual_pointer_v1_frame(p);
            }
            if (!strcmp(cmd, "click")) {
                wl_display_roundtrip(display);
                usleep(40000);
            }
            if (strcmp(cmd, "down")) {
                zwlr_virtual_pointer_v1_button(p, now_ms(), code, WL_POINTER_BUTTON_STATE_RELEASED);
                zwlr_virtual_pointer_v1_frame(p);
            }
        } else if (!strcmp(cmd, "scroll") && i + 1 < argc) {
            const int n = atoi(argv[++i]);
            zwlr_virtual_pointer_v1_axis(p, now_ms(), WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(10 * n));
            zwlr_virtual_pointer_v1_frame(p);
        } else if (!strcmp(cmd, "sleep") && i + 1 < argc) {
            wl_display_roundtrip(display);
            usleep((useconds_t)atoi(argv[++i]) * 1000);
        }
        wl_display_roundtrip(display);
    }
    zwlr_virtual_pointer_v1_destroy(p);
    wl_display_roundtrip(display);
    return 0;
}

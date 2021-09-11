#include "interface.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <gbm.h>

#include "obj/xdg-shell-stable-client-protocol.h"
#include "obj/linux-dmabuf-unstable-v1-client-protocol.h"
#include <wayland-client.h>

struct globals {
    struct wl_compositor *compositor;
    struct wl_registry *registry;
    struct xdg_wm_base *wm_base;
    struct zwp_linux_dmabuf_v1 *dmabuf;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct wl_buffer *buffer_dark;
    struct wl_buffer *buffer_light;
    struct wl_callback *frame_callback;
    struct wl_callback_listener frame_listener;
    struct gbm_device *gbm;
    int32_t width, height;
    int size_changed;
    int is_dark;
    int is_running;
};

static struct wl_buffer *make_buffer(struct gbm_device* gbm, struct zwp_linux_dmabuf_v1 *dmabuf, int width, int height,
                                     int is_dark) {
    // Linear is generally available; scanout buffers can be directly presented
    struct gbm_bo *bo = gbm_bo_create(gbm, width, height, GBM_FORMAT_XRGB8888,
                  GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT);
    if (!bo) {
        fprintf(stderr, "Failed at gbm_bo_create.\n");
        return NULL;
    }

    uint32_t stride = 0;
    void *map_handle = NULL;
    uint8_t *data = (uint8_t *)gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE,
                        &stride, &map_handle);
    if (!data) {
        gbm_bo_destroy(bo);
        fprintf(stderr, "Failed at to map GBM buffer object\n");
        return NULL;
    }
    for (int y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(data + y * stride);
        for (int x = 0; x < width; x++) {
             row[x] = is_dark ? 0xff000000 : 0xffffffff;
        }
    }
    gbm_bo_unmap(bo, map_handle);

    // stride need not match that used above
    stride = gbm_bo_get_stride(bo);
    int buffer_fd = gbm_bo_get_fd(bo);
    gbm_bo_destroy(bo);

    struct zwp_linux_buffer_params_v1 *params =
            zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, buffer_fd, 0,
            0, stride, 0, 0);
    struct wl_buffer *buffer = zwp_linux_buffer_params_v1_create_immed(params, width, height, GBM_FORMAT_XRGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);

    return buffer;
}

static void registry_add(void *data, struct wl_registry *wl_registry,
                         uint32_t name, const char *interface,
                         uint32_t version) {
    struct globals *glob = (struct globals *)data;

    if (!strcmp(xdg_wm_base_interface.name, interface)) {
        glob->wm_base =
            wl_registry_bind(glob->registry, name, &xdg_wm_base_interface, 1);
    }
    if (!strcmp(wl_compositor_interface.name, interface)) {
        glob->compositor =
            wl_registry_bind(glob->registry, name, &wl_compositor_interface, 1);
    }
    if (!strcmp(zwp_linux_dmabuf_v1_interface.name, interface)) {
        glob->dmabuf =
            wl_registry_bind(glob->registry, name, &zwp_linux_dmabuf_v1_interface, 3);
    }
}

static void registry_remove(void *data, struct wl_registry *wl_registry,
                            uint32_t name) {
    // maybe someday
}
static void update_surface(void *data, struct wl_callback *wl_callback,
                           uint32_t callback_data) {
    struct globals *glob = (struct globals *)data;
    if (glob->size_changed) {
        if (glob->buffer_dark) {
            wl_buffer_destroy(glob->buffer_dark);
        }
        if (glob->buffer_light) {
            wl_buffer_destroy(glob->buffer_light);
        }
        glob->buffer_light =
            make_buffer(glob->gbm, glob->dmabuf, glob->width, glob->height, 0);
        glob->buffer_dark =
            make_buffer(glob->gbm, glob->dmabuf, glob->width, glob->height, 1);

        glob->size_changed = 0;
    }
    if (!glob->buffer_dark || !glob->buffer_light) {
        return;
    }

    wl_surface_attach(glob->surface,
                      glob->is_dark ? glob->buffer_dark : glob->buffer_light, 0,
                      0);
    wl_surface_damage(glob->surface, 0, 0, glob->width, glob->height);

    if (glob->frame_callback) {
        // doesn't the server do this?
        wl_callback_destroy(glob->frame_callback);
    }
    glob->frame_callback = wl_surface_frame(glob->surface);
    wl_callback_add_listener(glob->frame_callback, &glob->frame_listener, glob);
    wl_surface_commit(glob->surface);
}

static void xdgsurf_configure(void *data, struct xdg_surface *xdg_surface,
                              uint32_t serial) {
    struct globals *glob = (struct globals *)data;
    xdg_surface_ack_configure(xdg_surface, serial);

    update_surface(glob, NULL, 0);
}
static void xdgtop_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                             int32_t width, int32_t height,
                             struct wl_array *states) {
    struct globals *glob = (struct globals *)data;
    if (width != glob->width || height != glob->height) {
        glob->size_changed = 1;
    }
    glob->width = width ? width : SMALL_WINDOW_SIZE;
    glob->height = height ? height : SMALL_WINDOW_SIZE;
}
static void xdgtop_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct globals *glob = (struct globals *)data;
    glob->is_running = 0;
}

int main(int argc, char **argv) {
    int camera_number = 0;
    if (argc != 2 || sscanf(argv[1], "%d", &camera_number) != 1) {
        fprintf(stderr, "Usage: %s camera_number\n", argv[0]);
        fprintf(stderr, "Wayland frontend for latency tester, using fixed buffers created with GBM\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Arguments:\n");
        fprintf(stderr, "  camera_number Which camera device to read from. "
                        "Should be /dev/videoN\n");
        return EXIT_FAILURE;
    }

    void *state = setup_backend(camera_number);
    if (!state) {
        fprintf(stderr, "Failed to open camera #%d", camera_number);
        return EXIT_FAILURE;
    }

    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to a display\n");
        return EXIT_FAILURE;
    }

    // Error handling often ignored for simplicity
    struct globals glob = {0};
    glob.width = SMALL_WINDOW_SIZE;
    glob.height = SMALL_WINDOW_SIZE;
    glob.size_changed = 1;
    glob.is_running = 1;
    glob.registry = wl_display_get_registry(display);
    struct wl_registry_listener reg_listen = {&registry_add, &registry_remove};
    wl_registry_add_listener(glob.registry, &reg_listen, &glob);
    wl_display_roundtrip(display); // Wait until registry filled
    if (!glob.dmabuf || !glob.wm_base || !glob.compositor) {
        fprintf(stderr,
                "Failed to acquire global: compositor %c linux-dmabuf %c wm_base %c\n",
                glob.compositor ? 'Y' : 'N', glob.dmabuf ? 'Y' : 'N',
                glob.wm_base ? 'Y' : 'N');
        return EXIT_FAILURE;
    }

    int drm_fd = open("/dev/dri/renderD128", O_RDWR);
    if (drm_fd == -1) {
        fprintf(stderr, "Failed to connect to DRM device at /dev/dri/renderD128\n");
        return EXIT_FAILURE;
    }
    glob.gbm = gbm_create_device(drm_fd);
    if (!glob.gbm) {
        fprintf(stderr, "Failed to create GBM device\n");
        return EXIT_FAILURE;
    }

    wl_display_dispatch(display); // wait for compositor to send requests

    // Make surface, then shell surface
    glob.surface = wl_compositor_create_surface(glob.compositor);
    if (!glob.surface) {
        fprintf(stderr, "Failed to create a surface\n");
        return EXIT_FAILURE;
    }

    glob.frame_listener.done = update_surface;

    glob.xdg_surface = xdg_wm_base_get_xdg_surface(glob.wm_base, glob.surface);
    struct xdg_surface_listener xdgsurf_listen = {.configure =
                                                      xdgsurf_configure};
    xdg_surface_add_listener(glob.xdg_surface, &xdgsurf_listen, &glob);
    struct xdg_toplevel *xdg_toplevel =
        xdg_surface_get_toplevel(glob.xdg_surface);
    struct xdg_toplevel_listener xdgtop_listen = {.configure = xdgtop_configure,
                                                  .close = xdgtop_close};
    xdg_toplevel_add_listener(xdg_toplevel, &xdgtop_listen, &glob);
    xdg_toplevel_set_title(xdg_toplevel, "wayland shm frontend");
    wl_surface_commit(glob.surface);

    struct timespec old_time;
    clock_gettime(CLOCK_MONOTONIC, &old_time);
    while (glob.is_running) {
        if (wl_display_dispatch_pending(display) == -1 ||
            wl_display_flush(display) == -1) {
            break;
        }

        struct pollfd fds[1];
        fds[0].fd = wl_display_get_fd(display);
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        poll(fds, sizeof fds / sizeof fds[0], 1); // wait up to 1 ms

        if (fds[0].revents && wl_display_dispatch(display) == -1) {
            break;
        }

        struct timespec new_time;
        clock_gettime(CLOCK_MONOTONIC, &new_time);
        double time_difference = (new_time.tv_sec - old_time.tv_sec) * 1.0 +
                                 (new_time.tv_nsec - old_time.tv_nsec) * 1e-9;
        if (time_difference > 0.001) {
            old_time = new_time;
            enum WhatToDo wtd = update_backend(state);
            int next_dark = wtd == DisplayDark;
            if (next_dark != glob.is_dark) {
                glob.is_dark = next_dark;
                // Update surface on color change
                update_surface(&glob, NULL, 1);
            }
        }
    }

    gbm_device_destroy(glob.gbm);
    close(drm_fd);
    wl_display_disconnect(display);
    cleanup_backend(state);
    return EXIT_SUCCESS;
}

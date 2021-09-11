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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include "obj/xdg-shell-stable-client-protocol.h"
#include <wayland-client.h>
#include <wayland-egl-core.h>

struct globals {
    struct wl_compositor *compositor;
    struct wl_registry *registry;
    struct xdg_wm_base *wm_base;
    struct wl_egl_window *window;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct wl_callback *frame_callback;
    struct wl_callback_listener frame_listener;
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    int32_t width, height;
    int size_changed;
    int is_dark;
    int is_running;
};

static void registry_add(void *data, struct wl_registry *wl_registry,
                         uint32_t name, const char *interface,
                         uint32_t version) {
    struct globals *glob = (struct globals *)data;

    if (!strcmp("xdg_wm_base", interface)) {
        glob->wm_base =
            wl_registry_bind(glob->registry, name, &xdg_wm_base_interface, 1);
    }
    if (!strcmp("wl_compositor", interface)) {
        glob->compositor =
            wl_registry_bind(glob->registry, name, &wl_compositor_interface, 1);
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
        wl_egl_window_resize(glob->window, glob->width, glob->height, 0, 0);
    }

    // While glClear is fairly efficient, the cost is still roughly proportional
    // to the window area.
    if (glob->is_dark) {
        glClearColor(0.0, 0.0, 0.0, 1.0);
    } else {
        glClearColor(1.0, 1.0, 1.0, 1.0);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    if (!eglSwapBuffers(glob->egl_display, glob->egl_surface)) {
        glob->is_running = 0;
        fprintf(stderr, "Failed to swap buffers\n");
        return;
    }
    //    wl_surface_damage(glob->surface, 0, 0, glob->width, glob->height);

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

    if (!glob->window) {
        // First configure, create the window
        glob->size_changed = 0;
        glob->window =
            wl_egl_window_create(glob->surface, glob->width, glob->height);
        if (!glob->window) {
            glob->is_running = 0;
            fprintf(stderr, "Failed to create EGL window\n");
            return;
        }
        glob->egl_surface =
            eglCreateWindowSurface(glob->egl_display, glob->egl_config,
                                   (EGLNativeWindowType)glob->window, NULL);
        if (!glob->egl_surface) {
            glob->is_running = 0;
            fprintf(stderr, "Failed to create EGL surface\n");
            return;
        }
        glob->egl_context = eglCreateContext(
            glob->egl_display, glob->egl_config, EGL_NO_CONTEXT, NULL);
        if (glob->egl_context == EGL_NO_CONTEXT) {
            glob->is_running = 0;
            fprintf(stderr, "Failed to create EGL context\n");
            return;
        }
        if (!eglMakeCurrent(glob->egl_display, glob->egl_surface,
                           glob->egl_surface,
                           glob->egl_context)) {
            glob->is_running = 0;
            fprintf(stderr,
                    "Failed to make EGL context current on EGL surface: %x\n",
                    eglGetError());
        }
    }

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
        fprintf(stderr, "Usage: latency_wayland_gl camera_number\n");
        fprintf(stderr, "Wayland frontend for latency tester\n");
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

    // Initialize EGL
    glob.egl_display =
        eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, NULL);
    if (!glob.egl_display) {
        fprintf(stderr, "Failed to create EGL display\n");
        return EXIT_FAILURE;
    }
    wl_display_roundtrip(display); // Wait until registry filled
    if (!glob.wm_base || !glob.compositor) {
        fprintf(stderr, "Failed to acquire global: compositor %c wm_base %c\n",
                glob.compositor ? 'Y' : 'N', glob.wm_base ? 'Y' : 'N');
        return EXIT_FAILURE;
    }

    int major_version = -1, minor_version = -1;
    if (!eglInitialize(glob.egl_display, &major_version, &minor_version)) {
        fprintf(stderr, "Failed to initialize EGL display\n");
        return EXIT_FAILURE;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "Failed to bind OpenGL API\n");
        return EXIT_FAILURE;
    }
    EGLint config_attrib_list[] = {EGL_SURFACE_TYPE,
                                   EGL_WINDOW_BIT,
                                   EGL_RED_SIZE,
                                   1,
                                   EGL_GREEN_SIZE,
                                   1,
                                   EGL_BLUE_SIZE,
                                   1,
                                   EGL_RENDERABLE_TYPE,
                                   EGL_OPENGL_BIT,
                                   EGL_NONE};
    int num_configs = 0;
    if (!eglChooseConfig(glob.egl_display, config_attrib_list, &glob.egl_config,
                         1, &num_configs) ||
        num_configs < 1) {
        fprintf(stderr, "Failed to get a valid EGL config\n");
        return EXIT_FAILURE;
    }

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

    wl_display_disconnect(display);
    cleanup_backend(state);
    return EXIT_SUCCESS;
}

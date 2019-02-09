#include "interface.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <xcb/xcb.h>

#define TARGET_PIXEL_X 100
#define TARGET_PIXEL_Y 100

struct state {
    xcb_connection_t *conn;
    xcb_drawable_t root;
    uint32_t black_pixel;
    uint32_t white_pixel;
    bool was_dark;
    int location;
    struct timespec last_time;
};

void *setup_backend(int camera) {
    struct state *s = calloc(1, sizeof(struct state));
    s->conn = xcb_connect(NULL, NULL);
    s->location = camera;
    fprintf(stdout, "Backend: watching pixel at (%d, %d)\n", s->location,
            s->location);

    xcb_screen_t *screen =
        xcb_setup_roots_iterator(xcb_get_setup(s->conn)).data;
    s->root = screen->root;

    s->black_pixel = screen->black_pixel;
    s->white_pixel = screen->white_pixel;

    xcb_flush(s->conn);

    s->was_dark = false;
    clock_gettime(CLOCK_MONOTONIC, &s->last_time);

    setvbuf(stdout, NULL, _IONBF, 0);
    return s;
}

enum WhatToDo update_backend(void *state) {
    struct state *s = (struct state *)state;

    xcb_get_image_cookie_t cookie =
        xcb_get_image(s->conn, XCB_IMAGE_FORMAT_Z_PIXMAP, s->root, s->location,
                      s->location, 1, 1, 0xffffffff);
    xcb_get_image_reply_t *image_reply =
        xcb_get_image_reply(s->conn, cookie, NULL);
    if (!image_reply) {
        printf("Failed to get image reply.\n");

        return DisplayDark;
    }
    uint8_t *data = xcb_get_image_data(image_reply);
    uint32_t length = xcb_get_image_data_length(image_reply);
    free(image_reply);
    if (length != 4) {
        printf("Response has unexpected shape\n");
        return DisplayDark;
    }
    uint8_t channels[4] = {data[0], data[1], data[2], data[3]};

    bool is_dark = ((uint32_t)channels[1] + (uint32_t)channels[2]) < 256;
    if (is_dark != s->was_dark) {
        s->was_dark = is_dark;
        struct timespec tp;
        clock_gettime(CLOCK_MONOTONIC, &tp);

        double offset = 1.0 * (tp.tv_sec - s->last_time.tv_sec) +
                        1e-9 * (tp.tv_nsec - s->last_time.tv_nsec);
        fprintf(stdout, "%s %10.5fms\n", is_dark ? "L->D" : "D->L",
                1e3 * offset);

        // On transition, introduce a random delay, so that we are not
        // phase locked by accident
        struct timespec delay;
        delay.tv_sec = 0;
        uint32_t r = rand();
        delay.tv_nsec = 40000000 + r % 40000000;
        nanosleep(&delay, NULL);

        clock_gettime(CLOCK_MONOTONIC, &s->last_time);
    }

    return is_dark ? DisplayLight : DisplayDark;
}

void cleanup_backend(void *state) {
    struct state *s = state;
    xcb_disconnect(s->conn);
    free(s);
}

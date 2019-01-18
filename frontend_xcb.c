#include "interface.h"

#include <stdio.h>
#include <stdlib.h>

#include <xcb/xcb.h>

int main(int argc, char **argv) {
    int camera_number = 0;
    if (argc != 2 || sscanf(argv[1], "%d", &camera_number) != 1) {
        fprintf(stderr, "Usage: latency_qt_xcb camera_number\n");
        fprintf(stderr, "XCB frontend for latency tester\n");
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

    // Boilerplate for xcb setup
    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    xcb_screen_t *screen =
        xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_drawable_t window = screen->root;
    xcb_gcontext_t foreground = xcb_generate_id(connection);
    uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    uint32_t values[2] = {screen->black_pixel, 0};
    xcb_create_gc(connection, foreground, window, mask, values);
    window = xcb_generate_id(connection);

    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = screen->white_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;

    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0,
                      0, SMALL_WINDOW_SIZE, SMALL_WINDOW_SIZE, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask,
                      values);

    /* Map the window on the screen and flush*/
    xcb_map_window(connection, window);
    xcb_flush(connection);

    int is_dark = 1;
    int quitting = 0;
    xcb_generic_event_t *event;
    int fdes = xcb_get_file_descriptor(connection);
    struct timespec timeout = {0, 1000000}; // 1ms tick, accuracy unimportant
    fd_set fds;
    while (1) {
        FD_ZERO(&fds);
        FD_SET(fdes, &fds);
        if (pselect(fdes + 1, &fds, NULL, NULL, &timeout, NULL) > 0) {
            // We assume no overflow
            if ((event = xcb_poll_for_event(connection))) {
                switch (event->response_type & ~0x80) {
                case XCB_EXPOSE:
                    values[0] =
                        is_dark ? screen->black_pixel : screen->white_pixel;
                    xcb_change_window_attributes(connection, window,
                                                 XCB_CW_BACK_PIXEL, values);
                    xcb_flush(connection);
                    break;
                case XCB_KEY_PRESS: {
                    xcb_key_press_event_t *key_event =
                        (xcb_key_press_event_t *)event;

                    /* ESC or Q, by keyboard position */
                    if (key_event->detail == 9 || key_event->detail == 24) {
                        xcb_destroy_window(connection, window);
                        quitting = 1;
                    }
                } break;
                default:
                    break; // Unimportant
                }
            }
        } else {
            // Poll the backend.
            enum WhatToDo wtd = update_backend(state);
            is_dark = wtd == DisplayDark;

            values[0] = is_dark ? screen->black_pixel : screen->white_pixel;
            xcb_change_window_attributes(connection, window, XCB_CW_BACK_PIXEL,
                                         values);
            xcb_clear_area(connection, 0, window, 0, 0, 0, 0);
            xcb_flush(connection);
        }
        if (quitting) {
            break;
        }
    }

    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);

    cleanup_backend(state);
    return EXIT_SUCCESS;
}

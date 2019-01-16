#include "interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <xcb/damage.h>
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
                      0, 400, 400, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, mask, values);

    /* Map the window on the screen and flush*/
    xcb_map_window(connection, window);
    xcb_flush(connection);

    int is_dark = 1;
    int quitting = 0;
    xcb_generic_event_t *event;

    const xcb_query_extension_reply_t *ext_damage_data =
        xcb_get_extension_data(connection, &xcb_damage_id);
    if (!ext_damage_data) {
        fprintf(stderr, "Failed to load xdamage, quitting\n");
        xcb_disconnect(connection);
        goto cleanup;
    }

    while ((event = xcb_wait_for_event(connection))) {
        switch (event->response_type & ~0x80) {
        case XCB_EXPOSE:
            values[0] = is_dark ? screen->black_pixel : screen->white_pixel;
            xcb_change_window_attributes(connection, window, XCB_CW_BACK_PIXEL,
                                         values);
            xcb_flush(connection);

            is_dark = !is_dark;

            break;
        case XCB_KEY_PRESS: {
            xcb_key_press_event_t *key_event = (xcb_key_press_event_t *)event;

            /* ESC or Q, by keyboard position */
            if (key_event->detail == 9 || key_event->detail == 24) {
                xcb_destroy_window(connection, window);
                quitting = 1;
            } else {
                values[0] = is_dark ? screen->black_pixel : screen->white_pixel;
                xcb_change_window_attributes(connection, window,
                                             XCB_CW_BACK_PIXEL, values);

                // xcb_damage_add(connection, window, 0);

                xcb_flush(connection);

                is_dark = !is_dark;
            }
        } break;

        default:
            /* Unknown event type, ignore it */
            break;
        }

        free(event);

        if (quitting) {
            break;
        }
    }

cleanup:
    cleanup_backend(state);
    return EXIT_SUCCESS;
}

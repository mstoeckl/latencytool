#include "interface.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define ESC "\x1b["
#if 0
// For normal color schemes (with a true black and true white)
#define WHITE (ESC "47m" ESC "2J")
#define BLACK (ESC "40m" ESC "2J")
#else
// For terminals with nonstandard palettes that nevertheless support truecolor
#define WHITE                                                                  \
    (ESC "48;5;255;255;255m" ESC "2J"                                          \
         "\n")
#define BLACK                                                                  \
    (ESC "48;5;0;0;0m" ESC "2J"                                                \
         "\n")
#endif

int main(int argc, char **argv) {
    int camera_number = 0;
    if (argc != 2 || sscanf(argv[1], "%d", &camera_number) != 1) {
        fprintf(stderr, "Usage: latency_qt_term camera_number\n");
        fprintf(stderr, "Terminal frontend for latency tester.\n");
        fprintf(stderr,
                "(It is recommended to pipe stdout to file, display stderr.\n");
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

    // Disable buffering, in case it was not already disabled
    setvbuf(stderr, NULL, _IONBF, 0);

    bool was_dark = true;
    fprintf(stderr, BLACK);

    // note: cancel the loop with Ctrl+C
    while (1) {
        // todo: 1ms pause?
        enum WhatToDo wtd = update_backend(state);
        bool is_dark = wtd == DisplayDark;
        if (is_dark != was_dark) {
            was_dark = is_dark;
            if (is_dark) {
                fprintf(stderr, BLACK);
            } else {
                fprintf(stderr, WHITE);
            }
        }
    }

    fprintf(stderr, ESC "0m\n");

    cleanup_backend(state);
    return EXIT_SUCCESS;
}

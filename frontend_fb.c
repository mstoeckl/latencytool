#include "interface.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int camera_number = 0;
    if (argc != 2 || sscanf(argv[1], "%d", &camera_number) != 1) {
        fprintf(stderr, "Usage: latency_qt_fb camera_number\n");
        fprintf(stderr, "Framebuffer frontend for latency tester\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Arguments:\n");
        fprintf(stderr, "  camera_number Which camera device to read from. "
                        "Should be /dev/videoN\n");
        return EXIT_FAILURE;
    }

    __uid_t uid = geteuid();
    if (uid == 0) {
        fprintf(stderr, "Please do not run this program as root.\n");
        return EXIT_FAILURE;
    }

    // Oddly enough, O_WRONLY fails even though we only mmap with PROT_WRITE
    int fbd = open("/dev/fb0", O_RDWR);
    if (fbd == -1) {
        fprintf(stderr, "Failed to open /dev/fb0, the first framebuffer. Are "
                        "you (and /dev/fb0) both in group 'video'?\n");
        return EXIT_FAILURE;
    }

    struct fb_var_screeninfo vari;
    if (ioctl(fbd, FBIOGET_VSCREENINFO, &vari) == -1) {
        fprintf(stderr, "Failed to read variable screen data\n");
        return EXIT_FAILURE;
    }
    struct fb_fix_screeninfo fixi;
    if (ioctl(fbd, FBIOGET_FSCREENINFO, &fixi) == -1) {
        fprintf(stderr, "Failed to read variable screen data\n");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "screen virt xres = %d, yres = %d\n", vari.xres_virtual,
            vari.yres_virtual);
    fprintf(stderr, "screen data length %d\n", fixi.smem_len);
    uint32_t *mem =
        (uint32_t *)mmap(0, fixi.smem_len, PROT_WRITE, MAP_SHARED, fbd, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "Failed to map screen device: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    void *state = setup_backend(camera_number);
    if (!state) {
        fprintf(stderr, "Failed to open camera #%d", camera_number);
        return EXIT_FAILURE;
    }

    bool was_dark = true;
    // note: cancel the loop with Ctrl+C
    while (1) {
        // todo: 1ms pause?
        enum WhatToDo wtd = update_backend(state);
        bool is_dark = wtd == DisplayDark;
        if (is_dark != was_dark) {
            was_dark = is_dark;

            memset(mem, is_dark ? 0 : 255, fixi.smem_len);
        }
    }

    munmap(mem, fixi.smem_len);
    close(fbd);

    cleanup_backend(state);
    return EXIT_SUCCESS;
}

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMALL_WINDOW_SIZE 400

enum WhatToDo { DisplayDark, DisplayLight };
void *setup_backend(int camera);
enum WhatToDo update_backend(void *state);
void cleanup_backend(void *state);

struct analysis {
    // Analysis of delays
    double current_camera_level; // What color did the camera last see?
    int showing_dark;            // What color should the screen show now?
    int want_switch;             // Is a time scheduled to switch screen colors?
    struct timespec capture_time;
    struct timespec next_switch_time;

    // Analysis of delays
    double *fir;
    int fir_head;
    int nframes;

    // To record raw data to file
    struct timespec setup_time;
    FILE *log;
};

int setup_analysis(struct analysis *a);
void cleanup_analysis(struct analysis *a);
enum WhatToDo update_analysis(struct analysis *s,
                              struct timespec measurement_time,
                              double measurement, double threshold);

inline int64_t get_delta_nsec(const struct timespec x0,
                              const struct timespec x1) {
    return (x1.tv_sec - x0.tv_sec) * 1000000000 + (x1.tv_nsec - x0.tv_nsec);
}

inline struct timespec advance_time(struct timespec previous,
                                    int64_t step_nsec) {
    struct timespec next;
    next.tv_sec = previous.tv_sec + step_nsec / 1000000000;
    next.tv_nsec = previous.tv_nsec + step_nsec % 1000000000;
    if (next.tv_nsec >= 1000000000) {
        next.tv_nsec -= 1000000000;
        next.tv_sec++;
    }
    return next;
}

#ifdef __cplusplus
}
#endif

#include "interface.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

// Tradeoff between statistical convergence and minimum time
#define FIR_LENGTH 100
/* Wait long enough for the brightness to stabilize. */
#define HOLD_MIN_TIME 0.040
#define HOLD_MAX_TIME 0.100

static double mean(double m0, double m1) { return m0 > 0. ? m1 / m0 : -1.; }

static double stdev(double m0, double m1, double m2) {
    return m0 > 2. ? sqrt((m2 - m1 * m1 / m0) / (m0 - 1.)) : -1.;
}

int setup_analysis(struct analysis *a) {
    a->fir = calloc(FIR_LENGTH, sizeof(double));
    if (!a->fir) {
        fprintf(stderr, "Failed to allocate ring buffer\n");
        return -1;
    }
    a->nframes = 0;
    a->fir_head = 0;
    char *logpath = getenv("LATENCYTOOL_LOG");
    if (logpath) {
        a->log = fopen(logpath, "w");
    } else {
        a->log = NULL;
    }

    clock_gettime(CLOCK_MONOTONIC, &a->setup_time);

    a->want_switch = false;
    // State initialization is arbitrary
    a->current_camera_level = 1.0;
    a->showing_dark = false;
    a->capture_time.tv_sec = 0;
    a->capture_time.tv_nsec = 0;

    return 0;
}
void cleanup_analysis(struct analysis *a) {
    free(a->fir);
    if (a->log) {
        fclose(a->log);
    }
}

static void update_fir(struct analysis *a, double delay, bool now_is_dark) {
    int idx = a->nframes % FIR_LENGTH;
    a->nframes++;
    a->fir[idx] = (now_is_dark ? delay : -delay) * 1e3;

    double n_ltd = 0., sum_ltd = 0., sum_ltd2 = 0.;
    double n_dtl = 0., sum_dtl = 0., sum_dtl2 = 0.;
    double min_tot = 1e100;
    double max_tot = -1e100;
    for (int i = 0; i < fmin(a->nframes - 2, FIR_LENGTH); i++) {
        double idel = a->fir[(FIR_LENGTH + a->nframes - i - 1) % FIR_LENGTH];
        if (idel > 0) {
            n_ltd += 1.;
            sum_ltd += idel;
            sum_ltd2 += idel * idel;
        } else {
            n_dtl += 1.;
            sum_dtl += -idel;
            sum_dtl2 += idel * idel;
        }
        max_tot = fmax(max_tot, fabs(idel));
        min_tot = fmin(min_tot, fabs(idel));
    }
    double n_tot = n_ltd + n_dtl, sum_tot = sum_ltd + sum_dtl,
           sum_tot2 = sum_ltd2 + sum_dtl2;

    double mean_ltd = mean(n_ltd, sum_ltd);
    double mean_dtl = mean(n_dtl, sum_dtl);
    double mean_tot = mean(n_tot, sum_tot);
    double std_ltd = stdev(n_ltd, sum_ltd, sum_ltd2);
    double std_dtl = stdev(n_dtl, sum_dtl, sum_dtl2);
    double std_tot = stdev(n_tot, sum_tot, sum_tot2);
    fprintf(stdout,
            "Net: (%5.2f < %5.2f±%4.2f < %5.2f)ms L->D: (%5.2f±%4.2f)ms; "
            "D->L: (%5.2f±%4.2f)ms\n",
            min_tot, mean_tot, std_tot, max_tot, mean_ltd, std_ltd, mean_dtl,
            std_dtl);
    fflush(stdout);
}

enum WhatToDo update_analysis(struct analysis *a, struct timespec meas_time,
                              double meas_level, double threshold) {
    struct timespec last_capture_time = a->capture_time;
    float last_camera_level = a->current_camera_level;
    a->current_camera_level = meas_level;
    a->capture_time = meas_time;

    bool was_dark = last_camera_level <= threshold;
    bool is_dark = a->current_camera_level <= threshold;

    struct timespec transition_time = last_capture_time;
    if (was_dark != is_dark) {
        // With a reasonably fast camera, the screen color change
        // curve can be reasonably well captured by linear interpolation.
        double t = (threshold - last_camera_level) /
                   (a->current_camera_level - last_camera_level);
        int64_t nsec_gap = get_delta_nsec(last_capture_time, a->capture_time);
        int64_t step = (int64_t)(t * nsec_gap);
        transition_time = advance_time(last_capture_time, step);

        // Delay computed relative to old switch time
        double delay =
            get_delta_nsec(a->next_switch_time, transition_time) * 1e-9;

        // Randomly pick the amount of time to wait after the transition,
        // to avoid accidentally synchronizing with something.
        double hold_time = HOLD_MIN_TIME + (HOLD_MAX_TIME - HOLD_MIN_TIME) *
                                               (rand() / (double)RAND_MAX);
        a->next_switch_time = advance_time(transition_time, hold_time * 1e9);
        a->want_switch = true;

        // Update the ringbuffer of transition delays
        update_fir(a, delay, is_dark);
    }

    // Change at requested time
    int display_transition = 0;
    if (a->want_switch &&
        get_delta_nsec(a->next_switch_time, a->capture_time) >= 0) {
        a->showing_dark = !is_dark;
        display_transition = a->showing_dark ? 1 : -1;
        a->want_switch = false;
    }

    // State logging
    if (a->log) {
        fprintf(a->log, "%.9f %.3f %d\n",
                get_delta_nsec(a->setup_time, meas_time) * 1e-9, meas_level,
                display_transition);
    }

end:
    return a->showing_dark ? DisplayDark : DisplayLight;
}

#include "opencv2/opencv.hpp"

#include "interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Tradeoff between statistical convergence and minimum time
#define FIR_LENGTH 100
// in [0,1]
#define THRESHOLD 0.4
/* 41 ms should be enough for a frame to stabilize, and is not low-integer
 * commeasurate with standard frame rates. */
#define HOLD_TIME (4 * 0.6180339887498949 / 60)

struct state {
    // Readout
    cv::VideoCapture *cap;
    cv::Mat graylevel;
    cv::Mat bgrframe;
    // Screen control logic!
    double current_camera_level; // What color did the camera last see?
    bool showing_dark;           // What color should the screen show now?
    bool want_switch;            // Is a time scheduled to switch screen colors?
    struct timespec capture_time;
    struct timespec next_switch_time;

    // Analysis of delays
    double fir[FIR_LENGTH];
    int fir_head;
    int nframes;

    // To record raw data to file
    struct timespec setup_time;
    FILE *log;
};

static void *isetup(int camera) {
    struct state *s = new struct state;
    s->cap = new cv::VideoCapture(camera, cv::CAP_V4L);
    if (!s->cap->isOpened()) {
        delete s->cap;
        delete s;
        return NULL;
    }

    fprintf(stderr, "Camera #%d loaded with backend '%s'\n", camera,
            s->cap->getBackendName().c_str());

    // V4L will typically select for the combination closest to this.
    // This gives us the fastest / lowest resolution result.
    bool w1 = s->cap->set(cv::CAP_PROP_FRAME_WIDTH, 1.);
    bool w2 = s->cap->set(cv::CAP_PROP_FRAME_HEIGHT, 1.);
    bool w3 = s->cap->set(cv::CAP_PROP_FPS, 1000.);
    fprintf(stderr, "Can set? Width %c Height %c Fps %c\n", w1 ? 'Y' : 'n',
            w2 ? 'Y' : 'n', w3 ? 'Y' : 'n');

    // We disable all automatic correction effects. While these may be helpful
    // getting a decent picture, alternating black-and-white images will only
    // confuse the predictor
    bool w4 = s->cap->set(cv::CAP_PROP_AUTO_EXPOSURE, 0.);
    bool w5 = s->cap->set(cv::CAP_PROP_AUTO_WB, 0.);
    fprintf(stderr, "Can set? Auto exposure %c Auto whitebalance %c\n",
            w4 ? 'Y' : 'n', w5 ? 'Y' : 'n');
    fprintf(stderr,
            "Be sure to check `v4l2-ctl -d %d -l` and disable auto gain\n",
            camera);

    double fps = s->cap->get(cv::CAP_PROP_FPS);
    double width = s->cap->get(cv::CAP_PROP_FRAME_WIDTH);
    double height = s->cap->get(cv::CAP_PROP_FRAME_HEIGHT);
    double autoexp = s->cap->get(cv::CAP_PROP_AUTO_EXPOSURE);
    double autowb = s->cap->get(cv::CAP_PROP_AUTO_WB);
    fprintf(
        stderr,
        "nominal fps=%.0f width=%.0f height=%.0f autoexp=%.0f autowb=%.0f\n",
        fps, width, height, autoexp, autowb);

    s->nframes = 0;
    s->fir_head = 0;
    s->want_switch = false;
    // State initialization is arbitrary
    s->current_camera_level = 1.0;
    s->showing_dark = false;
    s->capture_time.tv_sec = 0;
    s->capture_time.tv_nsec = 0;

    if (getenv("LATENCY_CV_LOG")) {
        s->log = fopen("/tmp/logfile.txt", "w");
    } else {
        s->log = NULL;
    }
    clock_gettime(CLOCK_MONOTONIC, &s->setup_time);
    return s;
}

static int64_t get_delta_nsec(const struct timespec x0,
                              const struct timespec x1) {
    return (x1.tv_sec - x0.tv_sec) * 1000000000 + (x1.tv_nsec - x0.tv_nsec);
}

static struct timespec advance_time(struct timespec previous,
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

static double mean(double m0, double m1) {
    return m0 > 0. ? m1 / m0 : std::numeric_limits<double>::quiet_NaN();
}

static double stdev(double m0, double m1, double m2) {
    return m0 > 2. ? std::sqrt((m2 - m1 * m1 / m0) / (m0 - 1.))
                   : std::numeric_limits<double>::quiet_NaN();
}

static void update_analysis(struct state *s, double delay, bool now_is_dark) {
    int idx = s->nframes % FIR_LENGTH;
    s->nframes++;
    s->fir[idx] = (now_is_dark ? delay : -delay) * 1e3;

    double n_ltd = 0., sum_ltd = 0., sum_ltd2 = 0.;
    double n_dtl = 0., sum_dtl = 0., sum_dtl2 = 0.;
    double min_tot = std::numeric_limits<double>::infinity();
    double max_tot = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < std::min(s->nframes - 2, FIR_LENGTH); i++) {
        double idel = s->fir[(FIR_LENGTH + s->nframes - i - 1) % FIR_LENGTH];
        if (idel > 0) {
            n_ltd += 1.;
            sum_ltd += idel;
            sum_ltd2 += idel * idel;
        } else {
            n_dtl += 1.;
            sum_dtl += -idel;
            sum_dtl2 += idel * idel;
        }
        max_tot = std::max(max_tot, std::abs(idel));
        min_tot = std::min(min_tot, std::abs(idel));
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
}

static bool iupdate(struct state *s) {
    // We return the opposite of the current camera state, and record/print
    // brightness transitions
    bool success = s->cap->read(s->bgrframe);
    if (!success) {
        return s->showing_dark;
    }

    double last_camera_level = s->current_camera_level;

    // Record frame capture time *before* postprocessing, as though it
    // had zero cost.
    struct timespec last_capture_time = s->capture_time;
    clock_gettime(CLOCK_MONOTONIC, &s->capture_time);

    cv::cvtColor(s->bgrframe, s->graylevel, cv::COLOR_BGR2GRAY);
    double level = cv::mean(s->graylevel)[0] / 255.0;
    s->current_camera_level = level;

    bool was_dark = last_camera_level <= THRESHOLD;
    bool is_dark = s->current_camera_level <= THRESHOLD;

    struct timespec transition_time = last_capture_time;
    if (was_dark != is_dark) {
        double t = (THRESHOLD - last_camera_level) /
                   (s->current_camera_level - last_camera_level);
        int64_t nsec_gap =
            1000000000 * (s->capture_time.tv_sec - last_capture_time.tv_sec) +
            (s->capture_time.tv_nsec - last_capture_time.tv_nsec);
        int64_t step = (int64_t)(t * nsec_gap);
        transition_time = advance_time(last_capture_time, step);

        // Delay computed relative to old switch time
        double delay =
            get_delta_nsec(s->next_switch_time, transition_time) * 1e-9;

        // With a reasonably fast camera, the screen color change
        // curve can be reasonably well captured by linear interpolation.
        s->next_switch_time = advance_time(transition_time, HOLD_TIME * 1e9);
        s->want_switch = true;

        // Update the ringbuffer of transition delays
        update_analysis(s, delay, is_dark);
    }

    // Change at requested time
    int display_transition = 0;
    if (s->want_switch &&
        get_delta_nsec(s->next_switch_time, s->capture_time) >= 0) {
        s->showing_dark = !is_dark;
        display_transition = s->showing_dark ? 1 : -1;
        s->want_switch = false;
    }

    // State logging
    if (s->log) {
        fprintf(s->log, "%.9f %.3f %d\n",
                get_delta_nsec(s->setup_time, s->capture_time) * 1e-9, level,
                display_transition);
    }

    return s->showing_dark;
}

extern "C" {

void *setup_backend(int camera) { return isetup(camera); }

enum WhatToDo update_backend(void *state) {
    struct state *s = (struct state *)state;
    if (iupdate(s)) {
        return DisplayDark;
    } else {
        return DisplayLight;
    }
}

void cleanup_backend(void *state) {
    if (state) {
        struct state *s = (struct state *)state;
        if (s->log)
            fclose(s->log);

        delete s->cap;
        delete s;
    }
}
}

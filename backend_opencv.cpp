#include "opencv2/opencv.hpp"

#include "interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// in [0,1], i.e, what brightness level is the light/dark cutoff
#define THRESHOLD 0.3

struct state {
    // Readout
    cv::VideoCapture *cap;
    cv::Mat graylevel;
    cv::Mat bgrframe;

    enum WhatToDo output_state;
    struct analysis control;
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

    if (setup_analysis(&s->control) < 0) {
        delete s->cap;
        delete s;
        return NULL;
    }
    s->output_state = DisplayLight;
    return s;
}

extern "C" {

void *setup_backend(int camera) { return isetup(camera); }

enum WhatToDo update_backend(void *state) {
    struct state *s = (struct state *)state;
    // We return the opposite of the current camera state, and record/print
    // brightness transitions
    bool success = s->cap->read(s->bgrframe);
    if (!success) {
        return s->output_state;
    }

    // Record frame capture time *before* postprocessing, as though it
    // had zero cost.
    struct timespec capture_time;
    clock_gettime(CLOCK_MONOTONIC, &capture_time);

    cv::cvtColor(s->bgrframe, s->graylevel, cv::COLOR_BGR2GRAY);
    double level = cv::mean(s->graylevel)[0] / 255.0;

    s->output_state =
        update_analysis(&s->control, capture_time, level, THRESHOLD);
    return s->output_state;
}

void cleanup_backend(void *state) {
    if (state) {
        struct state *s = (struct state *)state;
        cleanup_analysis(&s->control);

        delete s->cap;
        delete s;
    }
}
}

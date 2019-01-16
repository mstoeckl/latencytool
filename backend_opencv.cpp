#include "opencv2/opencv.hpp"

#include "interface.h"
#include <stdlib.h>
#include <time.h>

#define FIR_LENGTH 100
#define THRESHOLD 50

struct state {
    // Readout
    cv::VideoCapture *cap;
    cv::Mat graylevel;
    cv::Mat bgrframe;
    // Logic!
    bool camera_sees_dark;
    struct timespec fir[FIR_LENGTH];
    int fir_head;
    int nframes;
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

    double fps = s->cap->get(cv::CAP_PROP_FPS);
    double width = s->cap->get(cv::CAP_PROP_FRAME_WIDTH);
    double height = s->cap->get(cv::CAP_PROP_FRAME_HEIGHT);
    fprintf(stderr, "nominal fps=%.0f width=%.0f height=%.0f\n", fps, width,
            height);

    s->nframes = 0;
    s->fir_head = 0;
    s->camera_sees_dark = false;

    return s;
}

static bool get_frame_color(struct state *s, bool *seen_dark) {
    cv::Mat frame;
    bool success = s->cap->read(frame);
    if (!success) {
        return false;
    }

    // opencv autoconverts to BGR
    cv::Mat graylevel;
    cv::cvtColor(frame, graylevel, cv::COLOR_BGR2GRAY);
    cv::Scalar avgv = cv::mean(graylevel);

    // TODO: return the normalized average value,
    // so that transitions become obvious...
    double avg = avgv[0];
    if (avg > THRESHOLD) {
        *seen_dark = false;
    } else {
        *seen_dark = true;
    }
    return true;
}

static double get_time_difference(const struct timespec *x0,
                                  const struct timespec *x1) {
    return (x1->tv_sec - x0->tv_sec) * 1.0 + (x1->tv_nsec - x0->tv_nsec) * 1e-9;
}

static bool iupdate(struct state *s) {
    // We return the opposite of the current camera state, and record/print
    // brightness transitions
    bool success = s->cap->read(s->bgrframe);
    bool previously_saw_dark = s->camera_sees_dark;
    double level;
    if (!success) {
        goto end;
    }

    cv::cvtColor(s->bgrframe, s->graylevel, cv::COLOR_BGR2GRAY);
    level = cv::mean(s->graylevel)[0];
    // TODO: more detailed modeling, plot brightness curve!
    s->camera_sees_dark = level <= THRESHOLD;

    if (s->camera_sees_dark != previously_saw_dark) {
        int idx = s->nframes % FIR_LENGTH;
        s->nframes++;

        struct timespec old_time = s->fir[idx];
        clock_gettime(CLOCK_MONOTONIC, &s->fir[idx]);
        struct timespec new_time = s->fir[idx];
        struct timespec prev_time = s->fir[(idx + FIR_LENGTH - 1) % FIR_LENGTH];

        const char *transition =
            s->camera_sees_dark ? "(light->dark)" : "(dark->light)";

        if (s->nframes >= 2) {
            double switch_time = get_time_difference(&prev_time, &new_time);
            if (s->nframes > FIR_LENGTH) {
                double avg_switch_time =
                    get_time_difference(&old_time, &new_time) / FIR_LENGTH;
                fprintf(stdout, "Average: %8.3f ms; %s time = %8.3f\n",
                        avg_switch_time * 1e3, transition, switch_time * 1e3);
            } else {
                fprintf(stdout, "Average:   N/A    ms; %s time = %8.3f\n",
                        transition, switch_time * 1e3);
            }
        }
    }

end:
    return !s->camera_sees_dark;
}

extern "C" {
int magic_number() { return 42; }

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
    struct state *s = (struct state *)state;
    delete s->cap;
    delete s;
}
}

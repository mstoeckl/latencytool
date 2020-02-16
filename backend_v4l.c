#include "interface.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>

#include <linux/videodev2.h>

// Parameters for PS3 Eye
#define CAMERA_WIDTH 320
#define CAMERA_HEIGHT 240
#define CAMERA_PIXELFORMAT V4L2_PIX_FMT_SGBRG8
#define CAMERA_FIELD V4L2_FIELD_NONE
#define CAMERA_TIMEPERFRAME_NUM 1000
#define CAMERA_TIMEPERFRAME_DENOM 187000
#define THRESHOLD 0.25

// Tradeoff between statistical convergence and minimum time
#define FIR_LENGTH 100
/* 41 ms should be enough for a frame to stabilize, and is not low-integer
 * commeasurate with standard frame rates. */
// TODO: FIXME: THIS WARPS MEASUREMENTS, when the screen response time is
// similar
#define HOLD_TIME (4 * 0.6180339887498949 / 60)

#define NUM_BUFS 5

struct buf {
    void *data;
    size_t len;
};

struct state {
    int fd;
    struct buf bufs[NUM_BUFS];

    // Analysis of delays
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

static double mean(double m0, double m1) { return m0 > 0. ? m1 / m0 : -1.; }

static double stdev(double m0, double m1, double m2) {
    return m0 > 2. ? sqrt((m2 - m1 * m1 / m0) / (m0 - 1.)) : -1.;
}

static void update_analysis(struct state *s, double delay, bool now_is_dark) {
    int idx = s->nframes % FIR_LENGTH;
    s->nframes++;
    s->fir[idx] = (now_is_dark ? delay : -delay) * 1e3;

    double n_ltd = 0., sum_ltd = 0., sum_ltd2 = 0.;
    double n_dtl = 0., sum_dtl = 0., sum_dtl2 = 0.;
    double min_tot = 1e100;
    double max_tot = -1e100;
    for (int i = 0; i < fmin(s->nframes - 2, FIR_LENGTH); i++) {
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

static int ioctl_loop(int fd, unsigned long int req, void *arg) {
    // In case frontend has weird signal settings, check for EINTR
    while (1) {
        int r = ioctl(fd, req, arg);
        if (r == -1 && errno == EINTR) {
            continue;
        }
        return r;
    }
}

void *setup_backend(int camera) {
    struct state *s = calloc(1, sizeof(struct state));

    char devname[50];
    sprintf(devname, "/dev/video%d", camera);

    s->fd = open(devname, O_RDWR | O_NONBLOCK, 0);
    if (s->fd == -1) {
        fprintf(stderr, "Failed to open fd at %s: %s\n", devname,
                strerror(errno));
        goto fail_free;
    }

    struct v4l2_capability cap;
    if (ioctl_loop(s->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "Not a video device: %s\n", strerror(errno));
        goto fail_vfd;
    }

    uint32_t needed_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    if ((cap.capabilities & needed_caps) != needed_caps) {
        fprintf(stderr, "Lacks needed capabilities: %x\n", cap.capabilities);
        goto fail_vfd;
    }

    struct v4l2_streamparm sparm;
    memset(&sparm, 0, sizeof(sparm));
    sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sparm.parm.capture.timeperframe.numerator = CAMERA_TIMEPERFRAME_NUM;
    sparm.parm.capture.timeperframe.denominator = CAMERA_TIMEPERFRAME_DENOM;
    if (ioctl_loop(s->fd, VIDIOC_S_PARM, &sparm) < 0) {
        fprintf(stderr, "Failed to set FPS: %s\n", strerror(errno));
        goto fail_vfd;
    }
    if (ioctl_loop(s->fd, VIDIOC_G_PARM, &sparm) < 0) {
        fprintf(stderr, "Failed to get FPS: %s\n", strerror(errno));
        goto fail_vfd;
    }
    fprintf(stderr, "Camera FPS is: %f\n",
            sparm.parm.capture.timeperframe.denominator /
                (double)sparm.parm.capture.timeperframe.numerator);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAMERA_WIDTH;
    fmt.fmt.pix.height = CAMERA_HEIGHT;
    fmt.fmt.pix.pixelformat = CAMERA_PIXELFORMAT;
    fmt.fmt.pix.field = CAMERA_FIELD;
    if (ioctl_loop(s->fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "Failed to set video format: %s\n", strerror(errno));
        goto fail_vfd;
    }
    fprintf(stderr,
            "width=%d/%d height=%d/%d pixelfmt=%x/%x field=%d/%d colorspace=%d "
            "xfer_func=%d\n",
            fmt.fmt.pix.width, CAMERA_WIDTH, fmt.fmt.pix.height, CAMERA_HEIGHT,
            fmt.fmt.pix.pixelformat, CAMERA_PIXELFORMAT, fmt.fmt.pix.field,
            CAMERA_FIELD, fmt.fmt.pix.colorspace, fmt.fmt.pix.xfer_func);

    if (fmt.fmt.pix.width != CAMERA_WIDTH ||
        fmt.fmt.pix.height != CAMERA_HEIGHT ||
        fmt.fmt.pix.field != CAMERA_FIELD) {
        fprintf(stderr, "Video does not accept the hard-coded dimensions: %s\n",
                strerror(errno));
        goto fail_vfd;
    }

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = NUM_BUFS;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl_loop(s->fd, VIDIOC_REQBUFS, &reqbufs) < 0) {
        fprintf(stderr, "Failed to request buffers: %s\n", strerror(errno));
        goto fail_vfd;
    }

    for (int i = 0; i < NUM_BUFS; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl_loop(s->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "Failed to query buffers info %d/%d: %s\n", i,
                    NUM_BUFS, strerror(errno));
            goto fail_bufs;
        }
        if (buf.length == 0) {
            fprintf(stderr, "Zero buffer length\n");
            goto fail_bufs;
        }

        s->bufs[i].len = buf.length;
        s->bufs[i].data = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                               MAP_SHARED, s->fd, buf.m.offset);
        if (s->bufs[i].data == MAP_FAILED) {
            fprintf(stderr, "Failed to map buffer\n");
            goto fail_bufs;
        }

        if (ioctl_loop(s->fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "Failed to queue buffer: %s\n", strerror(errno));
            goto fail_bufs;
        }
    }

    enum v4l2_buf_type buftype;
    buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl_loop(s->fd, VIDIOC_STREAMON, &buftype) < 0) {
        fprintf(stderr, "Failed to start stream1: %s\n", strerror(errno));
        goto fail_bufs;
    }

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

    fprintf(stderr, "All set up\n");
    return s;
fail_bufs:
    for (int i = 0; i < NUM_BUFS; i++) {
        if (s->bufs[i].len) {
            munmap(s->bufs[i].data, s->bufs[i].len);
        }
    }
fail_vfd:
    close(s->fd);
fail_free:
    free(s);
    return NULL;
}

enum WhatToDo update_backend(void *state) {
    struct state *s = (struct state *)state;

    struct timespec m0, m1, m2, m3, m4;
    clock_gettime(CLOCK_MONOTONIC, &m0);

    struct pollfd pfd;
    pfd.fd = s->fd;
    pfd.events = POLLIN;
    int p = poll(&pfd, 1, 1); // 1 msec max timeout
    if (p < 0) {
        fprintf(stderr, "Poll failed: %s\n", strerror(errno));
        goto end;
    } else if (p == 0) {
        goto end;
    }

    clock_gettime(CLOCK_MONOTONIC, &m1);

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl_loop(s->fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) {
            goto end;
        } else {
            fprintf(stderr, "Dequeue failed: %s\n", strerror(errno));
            goto end;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &m2);

    struct timespec last_capture_time = s->capture_time;
    clock_gettime(CLOCK_MONOTONIC, &s->capture_time);
    //    fprintf(stderr, "%d\n", s->capture_time.tv_nsec);

    int length = s->bufs[buf.index].len;
    uint8_t *data = (uint8_t *)s->bufs[buf.index].data;

    // TODO: does interpreting the colors & Bayer layout make sense,
    // or should the fact that we just feed light/dark mean that
    // we can safely average pixel values and get 'good-enough' results?
    uint64_t net_val = 0;
    for (int i = 0; i < length; i++) {
        net_val += data[i];
    }
    double avg_val = net_val / (double)(length) / 255.0;

    double last_camera_level = s->current_camera_level;
    s->current_camera_level = avg_val;
    fprintf(stderr, "%f\n", s->current_camera_level);

    clock_gettime(CLOCK_MONOTONIC, &m3);

    if (ioctl_loop(s->fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "Requeue failed: %s\n", strerror(errno));
    }

    clock_gettime(CLOCK_MONOTONIC, &m4);

    //    fprintf(stderr, "%f %f %f %f\n", get_delta_nsec(m0, m1) * 1e-9,
    //            get_delta_nsec(m1, m2) * 1e-9, get_delta_nsec(m2, m3) * 1e-9,
    //            get_delta_nsec(m3, m4) * 1e-9);

    bool was_dark = last_camera_level <= THRESHOLD;
    bool is_dark = s->current_camera_level <= THRESHOLD;

    struct timespec transition_time = last_capture_time;
    if (was_dark != is_dark) {
        double t = (THRESHOLD - last_camera_level) /
                   (s->current_camera_level - last_camera_level);
        int64_t nsec_gap = get_delta_nsec(last_capture_time, s->capture_time);
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
                get_delta_nsec(s->setup_time, s->capture_time) * 1e-9, avg_val,
                display_transition);
    }

end:
    return s->showing_dark ? DisplayDark : DisplayLight;
}

void cleanup_backend(void *state) {
    struct state *s = state;
    close(s->fd);

    free(s);
}

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
#define THRESHOLD 0.3

#define NUM_BUFS 5

struct buf {
    void *data;
    size_t len;
};

struct state {
    int fd;
    struct buf bufs[NUM_BUFS];

    enum WhatToDo output_state;
    struct analysis control;
};

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

    s->output_state = DisplayLight;
    if (setup_analysis(&s->control) < 0) {
        goto fail_bufs;
    }

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

    struct timespec captime;
    clock_gettime(CLOCK_MONOTONIC, &captime);

    int length = s->bufs[buf.index].len;
    uint8_t *data = (uint8_t *)s->bufs[buf.index].data;

    // TODO: does interpreting the colors & Bayer layout make sense,
    // or should the fact that we just feed light/dark inputs mean that
    // we can safely average pixel values and get 'good-enough' results?
    uint64_t net_val = 0;
    for (int i = 0; i < length; i++) {
        net_val += data[i];
    }
    double avg_val = net_val / (double)(length) / 255.0;

    if (ioctl_loop(s->fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "Requeue failed: %s\n", strerror(errno));
    }

    s->output_state = update_analysis(&s->control, captime, avg_val, THRESHOLD);
end:
    return s->output_state;
}

void cleanup_backend(void *state) {
    struct state *s = state;

    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl_loop(s->fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < NUM_BUFS; i++) {
        if (s->bufs[i].len) {
            munmap(s->bufs[i].data, s->bufs[i].len);
        }
    }
    close(s->fd);
    cleanup_analysis(&s->control);

    free(s);
}

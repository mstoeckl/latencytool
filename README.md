# Latencytool

This is/will be a set of programs that can be used to measure the round-trip
latency between application, screen, and camera.

To use, attach USB camera, and run `latency_cv_qt N` where N is the
corresponding `/dev/videoN` device. This should open a window that is either
white or black; moving the camera directly in front of the window should start
rapidly switching the window color to the opposite of whatever the camera sees.
stdout will eventually print the average time for a color switch operation --
this is the round-trip latency. Tuning constants in `backend_opencv.cpp` may be
helpful. The V4L backend, on the other hand, will not work correctly unless
you adjust constants to match your camera.

It is recommended to test with as small a window as feasible, both to reduce
computational overhead, and because staring at large blinking lights of 10-12 Hz
for the several minutes needed to get a stable measurement will give you a
headache.

# Status

An OpenCV and a V4L backend have been written. Frontends are available for
terminal output, xcb, Wayland (Standard, OpenGL, GBM variants), /dev/fb0, and
Qt.

# Uses

Given a camera with a reasonably high framerate and known latency, one can
estimate the total delay between application rendering and the time the screen
update completes.

For example, with a PS3 Eye at 187 Hz, and a 60 Hz laptop monitor from 2015, we
can compare the amounts of lag introduced by various display server
combinations. With `latency_cv_qt`:

* X11, no compositing, small window: 25ms average round trip
* X11, no compositing, large window: 30ms average round trip
* kwin_wayland, small window: 45ms average round trip
* sway, small window: 47ms average round trip
* sway, small window, nested under X11: 43ms average round trip
* weston, small window: 41ms average round trip

With `latency_cv_xcb` on X11 without compositing, the average round trip time
is reduced to 24ms for small windows, and 25ms for large windows. It *also*
reduces the average round trip time for sway to 44ms, and weston to 41ms.

With `latency_cv_wayland` on sway, as a small window, 44ms average round
trip time is observed. 

With `latency_cv_fb`, 25ms average round trip time is observed.

# Installation

Current requirements are:

* GNU make
* pkgconfig
* opencv (tested with 4.0.1)
* Qt5 (tested with 5.12)
* libxcb (tested with 1.13.1)
* wayland (tested with 1.16.0)
* wayland-protocols (tested with 1.17.1)
* EGL (tested with 1.5)
* OpenGL (any version)
* gbm (tested with Mesa 21.2.1)
* V4L (as preferred opencv backend)
* Linux (for the framebuffer frontend, and the V4L backend)

To compile, run `make`.

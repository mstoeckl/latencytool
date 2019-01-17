# Latencytool

This is/will be a set of programs that can be used to measure the round-trip
latency between application, screen, and camera. 

To use, attach USB camera, and run `latency_cv_qt N` where N is the
corresponding `/dev/videoN` device. This should open a window that is either
white or black; moving the camera directly in front of the window should start
rapidly switching the window color to the opposite of whatever the camera sees.
stdout will eventually print the average time for a color switch operation --
this is the round-trip latency. Tuning constants in `backend_opencv.cpp` may be
helpful.

# Status

Currently, only an OpenCV backend, and Qt and xcb frontends have been written.

# Uses

Given a camera with a reasonably high framerate and known latency, one can
estimate the total delay between application rendering and the time the screen
update completes.

For example, with a PS3 Eye at 187 Hz, and a 60 Hz laptop monitor from 2015, we
can compare the amounts of lag introduced by various display server
combinations. With `latency_cv_qt`:

* X11, no compositing, small window: 25 ms average round trip
* X11, no compositing, large window: 30 ms average round trip
* kwin_wayland, small window: 45 ms average round trip
* sway, small window: 50ms average round trip
* sway, small window, nested under X11: 43ms average round trip
* weston, small window: 41ms average round trip

With `latency_cv_xcb`, the latency is reduced ~24 ms for small windows, and ~25 
for large windows.

# Installation

Current requirements are:

* GNU make
* pkgconfig
* opencv (tested with 4.0.1)
* V4L (as preferred opencv backend)
* Qt5 (tested with 5.12)
* libxcb (tested with 1.13.1)

To compile, run `make`.

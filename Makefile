# yes, overkill for setup

# Note: $(shell pkg-config --libs opencv4) gives *ALL* the libraries
cv_libs := -lopencv_core -lopencv_imgproc -lopencv_videoio
cv_cflags := $(shell pkg-config --cflags opencv4)
qt_libs := $(shell pkg-config --libs Qt5Widgets)
qt_cflags := $(shell pkg-config --cflags Qt5Widgets)
xcb_libs := $(shell pkg-config --libs xcb)
xcb_cflags := $(shell pkg-config --cflags xcb)
gl_libs :=  $(shell pkg-config --libs opengl egl wayland-egl)
gl_cflags :=  $(shell pkg-config --cflags opengl egl wayland-egl)
way_libs := $(shell pkg-config --libs wayland-client) -lrt
way_cflags := $(shell pkg-config --cflags wayland-client)
wayproto_dir := $(shell pkg-config --variable=pkgdatadir wayland-protocols)

flags=-O3 -ggdb3 -D_DEFAULT_SOURCE

all: latency_cv_xcb latency_cv_wayland latency_v4l_wayland_gl latency_v4l_wayland latency_v4l_xcb latency_cv_qt latency_cv_fb latency_cv_term latency_xcb_term

latency_cv_xcb: obj/frontend_xcb.o obj/backend_cv.o obj/common.o
	g++ $(flags) $(cv_libs) $(xcb_libs) -o latency_cv_xcb obj/frontend_xcb.o obj/backend_cv.o obj/common.o

latency_cv_wayland: obj/frontend_wayland.o obj/backend_cv.o obj/xdg-shell-stable-protocol.o obj/common.o
	g++ $(flags) $(cv_libs) $(way_libs) -o latency_cv_wayland obj/frontend_wayland.o obj/xdg-shell-stable-protocol.o obj/backend_cv.o obj/common.o

latency_cv_qt: obj/frontend_qt.o obj/backend_cv.o obj/common.o
	g++ $(flags) $(cv_libs) $(qt_libs) -o latency_cv_qt obj/frontend_qt.o obj/backend_cv.o obj/common.o

latency_cv_fb: obj/frontend_fb.o obj/backend_cv.o obj/common.o
	g++ $(flags) $(cv_libs) -o latency_cv_fb obj/frontend_fb.o obj/backend_cv.o obj/common.o

latency_cv_term: obj/frontend_term.o obj/backend_cv.o obj/common.o
	g++ $(flags) $(cv_libs) -o latency_cv_term obj/frontend_term.o obj/backend_cv.o obj/common.o

latency_v4l_wayland: obj/frontend_wayland.o obj/backend_v4l.o obj/xdg-shell-stable-protocol.o obj/common.o
	g++ $(flags) $(way_libs) -o latency_v4l_wayland obj/frontend_wayland.o obj/xdg-shell-stable-protocol.o obj/backend_v4l.o obj/common.o

latency_v4l_wayland_gl: obj/frontend_wayland_gl.o obj/backend_v4l.o obj/xdg-shell-stable-protocol.o obj/common.o
	g++ $(flags) $(way_libs) $(gl_libs) -o latency_v4l_wayland_gl obj/frontend_wayland_gl.o obj/xdg-shell-stable-protocol.o obj/backend_v4l.o obj/common.o

latency_v4l_xcb: obj/frontend_xcb.o obj/backend_v4l.o obj/xdg-shell-stable-protocol.o obj/common.o
	g++ $(flags) $(xcb_libs) -o latency_v4l_xcb obj/frontend_xcb.o obj/backend_v4l.o obj/common.o

latency_flicker_term: obj/frontend_term.o obj/backend_flicker.o
	g++ $(flags) -o latency_flicker_term obj/frontend_term.o obj/backend_flicker.o

latency_xcb_term: obj/frontend_term.o obj/backend_xcb.o
	g++ $(flags) $(xcb_libs) -o latency_xcb_term obj/frontend_term.o obj/backend_xcb.o

# Object files, in C (or C++ as libraries require)
obj/backend_cv.o: obj/.sentinel backend_opencv.cpp
	g++ $(flags) -c -fPIC $(cv_cflags) -o obj/backend_cv.o backend_opencv.cpp
obj/backend_flicker.o: obj/.sentinel backend_flicker.c
	gcc $(flags) -c -fPIC -o obj/backend_flicker.o backend_flicker.c
obj/backend_xcb.o: obj/.sentinel backend_xcb.c
	gcc $(flags) -c -fPIC $(xcb_cflags) -o obj/backend_xcb.o backend_xcb.c
obj/backend_v4l.o: obj/.sentinel backend_v4l.c
	gcc $(flags) -c -fPIC -o obj/backend_v4l.o backend_v4l.c

obj/frontend_qt.o: obj/.sentinel frontend_qt.cpp obj/frontend_qt.moc
	g++ $(flags) -c -fPIC $(qt_cflags) -o obj/frontend_qt.o frontend_qt.cpp
obj/frontend_qt.moc: obj/.sentinel frontend_qt.cpp
	moc $(qt_cflags) frontend_qt.cpp -o $@

obj/frontend_xcb.o: obj/.sentinel frontend_xcb.c
	gcc $(flags) -c -fPIC $(xcb_cflags) -o obj/frontend_xcb.o frontend_xcb.c

obj/frontend_wayland.o: obj/.sentinel frontend_wayland.c obj/xdg-shell-stable-client-protocol.h
	gcc $(flags) -c -fPIC $(way_cflags) -o obj/frontend_wayland.o frontend_wayland.c
obj/frontend_wayland_gl.o: obj/.sentinel frontend_wayland_gl.c obj/xdg-shell-stable-client-protocol.h
	gcc $(flags) -c -fPIC $(way_cflags) $(gl_cflags) -o obj/frontend_wayland_gl.o frontend_wayland_gl.c
obj/xdg-shell-stable-client-protocol.h: obj/.sentinel
	wayland-scanner client-header $(wayproto_dir)/stable/xdg-shell/xdg-shell.xml obj/xdg-shell-stable-client-protocol.h
obj/xdg-shell-stable-protocol.c: obj/.sentinel
	wayland-scanner private-code $(wayproto_dir)/stable/xdg-shell/xdg-shell.xml obj/xdg-shell-stable-protocol.c
obj/xdg-shell-stable-protocol.o: obj/.sentinel obj/xdg-shell-stable-protocol.c
	gcc $(flags) -c -fPIC $(way_cflags) -o obj/xdg-shell-stable-protocol.o obj/xdg-shell-stable-protocol.c

obj/frontend_fb.o: obj/.sentinel frontend_fb.c
	gcc $(flags) -c -fPIC -o obj/frontend_fb.o frontend_fb.c

obj/frontend_term.o: obj/.sentinel frontend_term.c
	gcc $(flags) -c -fPIC -o obj/frontend_term.o frontend_term.c

obj/common.o: obj/.sentinel common.c
	gcc $(flags) -c -fPIC -o obj/common.o common.c

# Misc

obj/.sentinel: 
	mkdir -p obj
	touch obj/.sentinel

clean:
	rm -f obj/*.h obj/*.c obj/*.o obj/*.moc latency_cv_xcb latency_cv_wayland latency_cv_qt latency_cv_fb latency_cv_term latency_flicker_term latency_xcb_term latency_v4l_wayland_gl latency_v4l_wayland latency_v4l_xcb

.PHONY: all clean

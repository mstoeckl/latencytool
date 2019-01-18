# yes, overkill for setup

# Note: $(shell pkg-config --libs opencv4) gives *ALL* the libraries
cv_libs := -lopencv_core -lopencv_imgproc -lopencv_videoio
cv_cflags := $(shell pkg-config --cflags opencv4)
qt_libs := $(shell pkg-config --libs Qt5Widgets)
qt_cflags := $(shell pkg-config --cflags Qt5Widgets)
xcb_libs := $(shell pkg-config --libs xcb)
xcb_cflags := $(shell pkg-config --cflags xcb)
way_libs := $(shell pkg-config --libs wayland-client) -lrt
way_cflags := $(shell pkg-config --cflags wayland-client)
wayproto_dir := $(shell pkg-config --variable=pkgdatadir wayland-protocols)

flags=-O0 -ggdb3

all: latency_cv_xcb latency_cv_wayland latency_cv_qt

latency_cv_xcb: obj/frontend_xcb.o obj/backend_cv.o
	g++ $(flags) $(cv_libs) $(xcb_libs) -o latency_cv_xcb obj/frontend_xcb.o obj/backend_cv.o

latency_cv_wayland: obj/frontend_wayland.o obj/backend_cv.o obj/xdg-shell-stable-protocol.o
	g++ $(flags) $(cv_libs) $(way_libs) -o latency_cv_wayland obj/frontend_wayland.o obj/xdg-shell-stable-protocol.o obj/backend_cv.o

latency_cv_qt: obj/frontend_qt.o obj/backend_cv.o
	g++ $(flags) $(cv_libs) $(qt_libs) -o latency_cv_qt obj/frontend_qt.o obj/backend_cv.o

# Object files, in C (or C++ as libraries require)
obj/backend_cv.o: obj/.sentinel backend_opencv.cpp
	g++ $(flags) -c -fPIC $(cv_cflags) -o obj/backend_cv.o backend_opencv.cpp

obj/frontend_qt.o: obj/.sentinel frontend_qt.cpp obj/frontend_qt.moc
	g++ $(flags) -c -fPIC $(qt_cflags) -o obj/frontend_qt.o frontend_qt.cpp
obj/frontend_qt.moc: obj/.sentinel frontend_qt.cpp
	moc $(qt_cflags) frontend_qt.cpp -o $@
	
obj/frontend_xcb.o: obj/.sentinel frontend_xcb.c
	gcc $(flags) -c -fPIC $(xcb_cflags) -o obj/frontend_xcb.o frontend_xcb.c


obj/frontend_wayland.o: obj/.sentinel frontend_wayland.c obj/xdg-shell-stable-client-protocol.h
	gcc $(flags) -c -fPIC $(way_cflags) -o obj/frontend_wayland.o frontend_wayland.c
obj/xdg-shell-stable-client-protocol.h: obj/.sentinel
	wayland-scanner client-header $(wayproto_dir)/stable/xdg-shell/xdg-shell.xml obj/xdg-shell-stable-client-protocol.h
obj/xdg-shell-stable-protocol.c: obj/.sentinel
	wayland-scanner private-code $(wayproto_dir)/stable/xdg-shell/xdg-shell.xml obj/xdg-shell-stable-protocol.c
obj/xdg-shell-stable-protocol.o: obj/.sentinel obj/xdg-shell-stable-protocol.c
	gcc $(flags) -c -fPIC $(way_cflags) -o obj/xdg-shell-stable-protocol.o obj/xdg-shell-stable-protocol.c
	
# Misc

obj/.sentinel: 
	mkdir -p obj
	touch obj/.sentinel

clean:
	rm -f obj/*.o obj/*.moc latency_cv_xcb latency_cv_wayland latency_cv_qt

.PHONY: all clean

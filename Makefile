# yes, overkill for setup

# Note: $(shell pkg-config --libs opencv4) gives *ALL* the libraries
cv_libs := -lopencv_core -lopencv_imgproc -lopencv_videoio
cv_cflags := $(shell pkg-config --cflags opencv4)
qt_libs := $(shell pkg-config --libs Qt5Widgets)
qt_cflags := $(shell pkg-config --cflags Qt5Widgets)
xcb_libs := $(shell pkg-config --libs xcb) $(shell pkg-config --libs xcb-damage)
xcb_cflags := $(shell pkg-config --cflags xcb) $(shell pkg-config --cflags xcb-damage)

all: latency_cv_xcb latency_cv_wayland latency_cv_qt

latency_cv_xcb: obj/frontend_xcb.o obj/backend_cv.o
	g++ $(cv_libs) $(xcb_libs) -o latency_cv_xcb obj/frontend_xcb.o obj/backend_cv.o

latency_cv_wayland: obj/frontend_wayland.o obj/backend_cv.o
	g++ $(cv_libs) -o latency_cv_wayland obj/frontend_wayland.o obj/backend_cv.o

latency_cv_qt: obj/frontend_qt.o obj/backend_cv.o
	g++ $(cv_libs) $(qt_libs) -o latency_cv_qt obj/frontend_qt.o obj/backend_cv.o

# Object files, in C (or C++ as libraries require)
obj/frontend_qt.o: obj/.sentinel frontend_qt.cpp obj/frontend_qt.moc
	g++ -c -fPIC $(qt_cflags) -o obj/frontend_qt.o frontend_qt.cpp

obj/frontend_wayland.o: obj/.sentinel frontend_wayland.c
	gcc -c -fPIC -o obj/frontend_wayland.o frontend_wayland.c

obj/frontend_xcb.o: obj/.sentinel frontend_xcb.c
	gcc -c -fPIC $(xcb_cflags) -o obj/frontend_xcb.o frontend_xcb.c

obj/backend_cv.o: obj/.sentinel backend_opencv.cpp
	g++ -c -fPIC $(cv_cflags) -o obj/backend_cv.o backend_opencv.cpp

obj/frontend_qt.moc: obj/.sentinel frontend_qt.cpp
	moc $(qt_cflags) frontend_qt.cpp -o $@

# Misc

obj/.sentinel: 
	mkdir -p obj
	touch obj/.sentinel

clean:
	rm -f obj/*.o obj/*.moc latency_cv_xcb latency_cv_wayland latency_cv_qt

.PHONY: all clean

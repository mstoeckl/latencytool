# yes, overkill for setup

cv_link := $(shell pkg-config --libs opencv4)
cv_include := $(shell pkg-config --cflags opencv4)
qt_link := $(shell pkg-config --libs Qt5Widgets)
qt_include := $(shell pkg-config --cflags Qt5Widgets)

all: latency_cv_xlib latency_cv_wayland latency_cv_qt

latency_cv_xlib: obj/frontend_xlib.o obj/backend_cv.o
	g++ $(cv_link) -o latency_cv_xlib obj/frontend_xlib.o obj/backend_cv.o

latency_cv_wayland: obj/frontend_wayland.o obj/backend_cv.o
	g++ $(cv_link) -o latency_cv_wayland obj/frontend_wayland.o obj/backend_cv.o

latency_cv_qt: obj/frontend_qt.o obj/backend_cv.o
	g++ $(cv_link) $(qt_link) -o latency_cv_qt obj/frontend_qt.o obj/backend_cv.o

# Object files, in C (or C++ as libraries require)
obj/frontend_qt.o: obj/.sentinel frontend_qt.cpp obj/frontend_qt.moc
	g++ -c -fPIC $(qt_include) -o obj/frontend_qt.o frontend_qt.cpp

obj/frontend_wayland.o: obj/.sentinel frontend_wayland.c
	gcc -c -fPIC -o obj/frontend_wayland.o frontend_wayland.c

obj/frontend_xlib.o: obj/.sentinel frontend_xlib.c
	gcc -c -fPIC -o obj/frontend_xlib.o frontend_xlib.c

obj/backend_cv.o: obj/.sentinel backend_opencv.cpp
	g++ -c -fPIC $(cv_link) $(cv_include) -o obj/backend_cv.o backend_opencv.cpp

obj/frontend_qt.moc: obj/.sentinel frontend_qt.cpp
	moc $(qt_include) frontend_qt.cpp -o $@

# Misc

obj/.sentinel: 
	mkdir -p obj
	touch obj/.sentinel

clean:
	rm -f obj/*.o obj/*.moc latency_cv_xlib latency_cv_wayland latency_cv_qt

.PHONY: all clean

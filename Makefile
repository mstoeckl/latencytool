# yes, overkill for setup

# cv_link := $(shell pkg-config --libs opencv4)
# cv_include = $(shell pkg-config --cflags-only-I opencv4)
# qt_link = $(shell pkg-config --libs Qt5Widgets)
# qt_include = $(shell pkg-config --cflags-only-I Qt5Widgets)

cv_link=-lopencv_gapi -lopencv_stitching -lopencv_aruco -lopencv_bgsegm -lopencv_bioinspired -lopencv_ccalib -lopencv_dnn_objdetect -lopencv_dpm -lopencv_face -lopencv_freetype -lopencv_fuzzy -lopencv_hdf -lopencv_hfs -lopencv_img_hash -lopencv_line_descriptor -lopencv_reg -lopencv_rgbd -lopencv_saliency -lopencv_stereo -lopencv_structured_light -lopencv_phase_unwrapping -lopencv_superres -lopencv_optflow -lopencv_surface_matching -lopencv_tracking -lopencv_datasets -lopencv_text -lopencv_dnn -lopencv_plot -lopencv_videostab -lopencv_video -lopencv_viz -lopencv_xfeatures2d -lopencv_shape -lopencv_ml -lopencv_ximgproc -lopencv_xobjdetect -lopencv_objdetect -lopencv_calib3d -lopencv_features2d -lopencv_highgui -lopencv_videoio -lopencv_imgcodecs -lopencv_flann -lopencv_xphoto -lopencv_photo -lopencv_imgproc -lopencv_core
cv_include=-I/usr/include/opencv4/opencv -I/usr/include/opencv4
qt_link=-lQt5Widgets -lQt5Gui -lQt5Core
qt_include=-DQT_WIDGETS_LIB -I/usr/include/qt/QtWidgets -I/usr/include/qt -I/usr/include/qt/QtCore -DQT_GUI_LIB -I/usr/include/qt/QtGui -DQT_CORE_LIB

all: latency_cv_xlib latency_cv_wayland latency_cv_qt
	
.PHONY: all

latency_cv_xlib: obj/frontend_xlib.o obj/backend_cv.o
	g++ $(cv_link) -o latency_cv_xlib obj/frontend_xlib.o obj/backend_cv.o

latency_cv_wayland: obj/frontend_wayland.o obj/backend_cv.o
	g++ $(cv_link) -o latency_cv_wayland obj/frontend_wayland.o obj/backend_cv.o

latency_cv_qt: obj/frontend_qt.o obj/backend_cv.o
	g++ $(cv_link) $(qt_link) -o latency_cv_qt obj/frontend_qt.o obj/backend_cv.o

# Object files, in C (or C++ as libraries require)
obj/frontend_qt.o: obj frontend_qt.cpp obj/frontend_qt.moc
	g++ -shared -fPIC $(qt_include) -o obj/frontend_qt.o frontend_qt.cpp

obj/frontend_wayland.o: obj frontend_wayland.c
	gcc -shared -fPIC -o obj/frontend_wayland.o frontend_wayland.c

obj/frontend_xlib.o: obj frontend_xlib.c
	gcc -shared -fPIC -o obj/frontend_xlib.o frontend_xlib.c
	
obj/backend_cv.o: obj backend_opencv.cpp
	g++ -shared -fPIC $(cv_link) $(cv_include) -o obj/backend_cv.o backend_opencv.cpp

obj/frontend_qt.moc: frontend_qt.cpp
	moc $(qt_include) $< -o $@
	
# Misc
	
obj: 
	mkdir obj

.PHONY: clean
clean:
	rm -f obj/*.o latency_cv_xlib latency_cv_wayland latency_cv_qt
#include "interface.h"

#include <iostream>
#include <stdlib.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>
#include <QWidget>

class MainWindow : public QWidget {
    Q_OBJECT
  public:
    MainWindow(void *s) : QWidget(NULL) {
        state = s;
        screen_dark = true;
        setWindowFlag(Qt::Window);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_PaintUnclipped);
        // TODO: add mode with WA_PaintOnScreen ?

        timer.setSingleShot(false);
        timer.setInterval(2);
        timer.start();
        connect(&timer, &QTimer::timeout, this, &MainWindow::checkCamera);
    }

    virtual void paintEvent(QPaintEvent *event) override {
        QPainter p(this);
        p.fillRect(this->rect(), screen_dark ? Qt::black : Qt::white);
    }
  public slots:
    void checkCamera() {
        enum WhatToDo wtd = update_backend(state);
        bool next_dark = wtd == DisplayDark;
        if (next_dark != screen_dark) {
            screen_dark = next_dark;
            update();
        }
    }

  private:
    QTimer timer;
    void *state;
    bool screen_dark;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    // TODO: handle arguments, camera number, etc.
    QCommandLineParser parser;
    parser.setApplicationDescription("Qt frontend for latency tester");
    parser.addHelpOption();
    parser.addPositionalArgument(
        "camera_number",
        "Which camera device to read from. Should be /dev/videoN");
    bool succeeded = parser.parse(app.arguments());
    if (!succeeded) {
        return EXIT_FAILURE;
    }

    QString cn = parser.positionalArguments()[0];
    bool ok;
    int camera_number = cn.toInt(&ok);
    if (!ok) {
        parser.showHelp();
        return EXIT_FAILURE;
    }

    void *state = setup_backend(camera_number);
    if (!state) {
        qDebug("Failed to open camera #%d", camera_number);
        return EXIT_FAILURE;
    }

    MainWindow window(state);
    window.setVisible(true);
    int ret = app.exec();
    cleanup_backend(state);

    // TODO: print final distribution summary!
    return ret;
}

#include "obj/frontend_qt.moc"

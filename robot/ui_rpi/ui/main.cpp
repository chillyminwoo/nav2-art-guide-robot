#include <QApplication>
#include <QScreen>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setFont(QFont("Sans", 12));

    MainWindow w;

    w.resize(800, 480);
    w.show();

    return app.exec();
}

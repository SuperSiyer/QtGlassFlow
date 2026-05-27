#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    MainWindow w;
    w.setWindowTitle("QtGlassFlow Demo");
    w.resize(1100, 700);
    w.show();

    return app.exec();
}

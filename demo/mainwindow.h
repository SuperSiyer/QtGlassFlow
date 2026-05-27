#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QSlider;
class QLabel;
class QtGlassFlowScene;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    void setupUI();
    void applyParams();

    QtGlassFlowScene *m_scene;

    // 参数面板滑块
    QSlider *m_refractionSlider;
    QSlider *m_blurSlider;
    QSlider *m_noiseSlider;
    QSlider *m_attractionSlider;
    QSlider *m_powerSlider;
};

#endif // MAINWINDOW_H

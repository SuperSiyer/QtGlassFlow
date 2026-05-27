#include "mainwindow.h"
#include "qtglassflowscene.h"

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QStatusBar>
#include <QFont>

static const QString kBgPath =
    QStringLiteral("/usr/share/backgrounds/1-warty-final-ubuntukylin.jpg");

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_scene(0)
    , m_refractionSlider(0)
    , m_blurSlider(0)
    , m_noiseSlider(0)
    , m_attractionSlider(0)
    , m_powerSlider(0)
{
    setupUI();
    setWindowTitle(QString::fromUtf8("LiquidGlass Demo"));
    resize(1100, 700);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === 左侧：LiquidGlassScene ===
    m_scene = new QtGlassFlowScene(this);
    m_scene->setBackgroundImage(kBgPath);

    // 4 个可拖拽玻璃对象
    m_scene->addGlassObject(QPointF(120, 100), QSizeF(140, 140), 3.0f,
                            QString::fromUtf8("Glass A"));
    m_scene->addGlassObject(QPointF(350, 80),  QSizeF(120, 120), 4.0f,
                            QString::fromUtf8("Glass B"));
    m_scene->addGlassObject(QPointF(200, 320), QSizeF(160, 100), 2.5f,
                            QString::fromUtf8("Glass C"));
    m_scene->addGlassObject(QPointF(500, 250), QSizeF(100, 100), 6.0f,
                            QString::fromUtf8("Glass D"));

    mainLayout->addWidget(m_scene, 1);

    // === 右侧：参数面板 ===
    QWidget *paramPanel = new QWidget();
    paramPanel->setFixedWidth(260);
    paramPanel->setStyleSheet(
        "QWidget { background-color: #2c2c2c; color: #eaeaea; }"
        "QLabel { color: #eaeaea; font-size: 12px; }"
        "QSlider::groove:horizontal { height: 4px; background: #555;"
        " border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #d0d0d0; width: 14px;"
        " margin: -6px 0; border-radius: 7px; }"
        "QSlider::sub-page:horizontal { background: #6aa3ff; border-radius: 2px; }");

    QVBoxLayout *paramLayout = new QVBoxLayout(paramPanel);
    paramLayout->setContentsMargins(14, 14, 14, 14);
    paramLayout->setSpacing(10);

    QLabel *panelTitle = new QLabel(QString::fromUtf8("液态玻璃参数"));
    QFont titleFont = panelTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    panelTitle->setFont(titleFont);
    paramLayout->addWidget(panelTitle);

    auto addSlider = [&](const QString &label, int mn, int mx, int defVal) -> QSlider* {
        QLabel *lbl = new QLabel(label);
        QSlider *slider = new QSlider(Qt::Horizontal);
        slider->setRange(mn, mx);
        slider->setValue(defVal);
        QLabel *valLbl = new QLabel(QString::number(defVal));
        valLbl->setMinimumWidth(36);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        connect(slider, &QSlider::valueChanged, [this, valLbl](int v) {
            valLbl->setText(QString::number(v));
            applyParams();
        });

        paramLayout->addWidget(lbl);
        QHBoxLayout *h = new QHBoxLayout();
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        h->addWidget(slider, 1);
        h->addWidget(valLbl);
        paramLayout->addLayout(h);
        return slider;
    };

    m_refractionSlider = addSlider(QString::fromUtf8("折射强度 (×0.1)"), 0, 30, 30);
    m_blurSlider       = addSlider(QString::fromUtf8("模糊半径 (×0.1)"), 0, 100, 0);
    m_noiseSlider      = addSlider(QString::fromUtf8("噪声量 (×0.001)"), 0, 100, 0);
    m_attractionSlider = addSlider(QString::fromUtf8("吸引距离 (px)"), 0, 400, 400);
    m_powerSlider      = addSlider(QString::fromUtf8("超椭圆幂 (×0.1)"), 20, 100, 100);

    paramLayout->addStretch(1);

    QLabel *hint = new QLabel(QString::fromUtf8(
        "提示：拖拽玻璃对象，靠近时产生粘性连接效果。\n"
        "调节右侧滑块改变全局渲染参数。"));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #888; font-size: 11px;");
    paramLayout->addWidget(hint);

    mainLayout->addWidget(paramPanel);

    // === 状态栏 ===
    QLabel *statusLabel = new QLabel(
        QString::fromUtf8("拖拽玻璃对象 | 靠近产生粘性连接"));
    statusBar()->addWidget(statusLabel, 1);

    // 应用初始参数
    applyParams();
}

void MainWindow::applyParams()
{
    if (!m_scene || !m_refractionSlider)
        return;

    m_scene->setRefractionPower(m_refractionSlider->value() / 10.0f);
    m_scene->setBlurRadius(m_blurSlider->value() / 10.0f);
    m_scene->setNoiseAmount(m_noiseSlider->value() / 1000.0f);
    m_scene->setAttractionDistance(static_cast<float>(m_attractionSlider->value()));
    m_scene->setPowerFactor(m_powerSlider->value() / 10.0f);
}

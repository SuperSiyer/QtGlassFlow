#ifndef QTGLASSFLOWSCENE_H
#define QTGLASSFLOWSCENE_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLFramebufferObject>
#include <QVector>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QElapsedTimer>

class QTimer;

class QtGlassFlowScene : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    enum State { Normal, Hovered, Pressed };

    struct GlassObject {
        QPointF position;      // top-left in widget pixel coordinates
        QSizeF size;           // pixel size
        float powerFactor;     // superellipse exponent
        QString text;          // optional label
        State state;
        bool dragging;
        QPointF dragOffset;

        GlassObject()
            : powerFactor(3.0f), state(Normal), dragging(false) {}
    };

    struct Connection {
        int objA;
        int objB;
        float strength; // 0~1
    };

    explicit QtGlassFlowScene(QWidget *parent = nullptr);
    ~QtGlassFlowScene();

    int addGlassObject(const QPointF &pos,
                       const QSizeF &size,
                       float power = 3.0f,
                       const QString &text = QString());
    void setBackgroundImage(const QString &path);

    // Global tunables
    void setPowerFactor(float v);
    void setRefractionPower(float v);
    void setBlurRadius(float v);
    void setBlurIterations(int v);
    void setNoiseAmount(float v);
    void setAttractionDistance(float v);

signals:
    void objectClicked(int index);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool compileProgram(QOpenGLShaderProgram *prog,
                        const QString &vertPath,
                        const QString &fragPath);
    void initQuad();
    void drawFullscreenQuad(QOpenGLShaderProgram *prog);
    void createFBOs(int w, int h);
    void destroyFBOs();
    void loadBackgroundTexture();
    void renderBackgroundToFbo();
    void runBlurPass();
    void blitTextureToScreen(GLuint tex);
    void renderGlassObject(int index);
    void updateConnections();
    bool hitTest(int objIndex, const QPointF &widgetPos) const;

    // Convert widget pixel position to NDC [-1,1]
    QPointF widgetToNdc(const QPointF &p) const;
    QSizeF widgetSizeToNdc(const QSizeF &s) const;

    // FBOs
    QOpenGLFramebufferObject *m_sceneFbo;
    QOpenGLFramebufferObject *m_blurPingFbo;
    QOpenGLFramebufferObject *m_blurPongFbo;

    // Shaders
    QOpenGLShaderProgram *m_blitShader;
    QOpenGLShaderProgram *m_blurShader;
    QOpenGLShaderProgram *m_glassShader;

    // Geometry
    QOpenGLBuffer m_quadVbo;
    bool m_quadInitialized;

    // Background
    GLuint m_bgTexture;
    int m_bgWidth;
    int m_bgHeight;
    QString m_bgPath;
    bool m_bgDirty;

    // Data
    QVector<GlassObject> m_objects;
    QVector<Connection> m_connections;

    // Refraction parameters
    float m_refractionA;
    float m_refractionB;
    float m_refractionC;
    float m_refractionD;
    float m_fPower;

    // Blur parameters
    float m_blurRadius;
    int m_blurIterations;

    // Visual tuning
    float m_noiseAmount;
    float m_attractionDist;
    float m_globalPower;

    // Animation
    QTimer *m_timer;
    QElapsedTimer m_clock;

    // Interaction
    int m_hoveredIndex;
    int m_dragIndex;
};

#endif // QTGLASSFLOWSCENE_H

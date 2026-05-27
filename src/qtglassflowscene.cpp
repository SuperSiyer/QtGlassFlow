#include "qtglassflowscene.h"

#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVector2D>
#include <QVector3D>
#include <QtMath>
#include <QDebug>
#include <QOpenGLPixelTransferOptions>
#include <QSurfaceFormat>
#include <cmath>

namespace {

// Simple fullscreen blit shaders, compiled from source strings to avoid
// shipping yet another pair of resource files.
static const char *kBlitVertSrc =
    "#version 120\n"
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "    v_texcoord = a_texcoord;\n"
    "}\n";

static const char *kBlitFragSrc =
    "#version 120\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_texture, v_texcoord);\n"
    "}\n";

// Fullscreen NDC quad: pos.xy + uv.xy interleaved (4 floats per vertex)
static const float kQuadVerts[] = {
    // pos        uv
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
};

} // namespace

QtGlassFlowScene::QtGlassFlowScene(QWidget *parent)
    : QOpenGLWidget(parent),
      m_sceneFbo(nullptr),
      m_blurPingFbo(nullptr),
      m_blurPongFbo(nullptr),
      m_blitShader(nullptr),
      m_blurShader(nullptr),
      m_glassShader(nullptr),
      m_quadVbo(QOpenGLBuffer::VertexBuffer),
      m_quadInitialized(false),
      m_bgTexture(0),
      m_bgWidth(0),
      m_bgHeight(0),
      m_bgDirty(false),
      m_refractionA(0.7f),
      m_refractionB(2.3f),
      m_refractionC(5.2f),
      m_refractionD(6.9f),
      m_fPower(1.0f),
      m_blurRadius(2.0f),
      m_blurIterations(2),
      m_noiseAmount(0.06f),
      m_attractionDist(160.0f),
      m_globalPower(3.0f),
      m_timer(nullptr),
      m_hoveredIndex(-1),
      m_dragIndex(-1)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    QSurfaceFormat fmt = format();
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    fmt.setVersion(2, 1);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(fmt);
}

QtGlassFlowScene::~QtGlassFlowScene()
{
    makeCurrent();
    destroyFBOs();
    if (m_bgTexture) {
        glDeleteTextures(1, &m_bgTexture);
        m_bgTexture = 0;
    }
    delete m_blitShader;
    delete m_blurShader;
    delete m_glassShader;
    if (m_quadVbo.isCreated())
        m_quadVbo.destroy();
    doneCurrent();
}

int QtGlassFlowScene::addGlassObject(const QPointF &pos, const QSizeF &size,
                                     float power, const QString &text)
{
    GlassObject obj;
    obj.position = pos;
    obj.size = size;
    obj.powerFactor = power;
    obj.text = text;
    m_objects.append(obj);
    update();
    return m_objects.size() - 1;
}

void QtGlassFlowScene::setBackgroundImage(const QString &path)
{
    m_bgPath = path;
    m_bgDirty = true;
    if (isValid()) {
        makeCurrent();
        loadBackgroundTexture();
        doneCurrent();
        update();
    }
}

void QtGlassFlowScene::setPowerFactor(float v)         { m_globalPower    = v; update(); }
void QtGlassFlowScene::setRefractionPower(float v)     { m_fPower         = v; update(); }
void QtGlassFlowScene::setBlurRadius(float v)          { m_blurRadius     = v; update(); }
void QtGlassFlowScene::setBlurIterations(int v)        { m_blurIterations = qMax(1, v); update(); }
void QtGlassFlowScene::setNoiseAmount(float v)         { m_noiseAmount    = v; update(); }
void QtGlassFlowScene::setAttractionDistance(float v)  { m_attractionDist = v; update(); }

bool QtGlassFlowScene::compileProgram(QOpenGLShaderProgram *prog,
                                      const QString &vertPath,
                                      const QString &fragPath)
{
    if (!prog->addShaderFromSourceFile(QOpenGLShader::Vertex, vertPath)) {
        qWarning() << "vertex shader compile failed:" << vertPath << prog->log();
        return false;
    }
    if (!prog->addShaderFromSourceFile(QOpenGLShader::Fragment, fragPath)) {
        qWarning() << "fragment shader compile failed:" << fragPath << prog->log();
        return false;
    }
    prog->bindAttributeLocation("a_position", 0);
    prog->bindAttributeLocation("a_texcoord", 1);
    if (!prog->link()) {
        qWarning() << "shader link failed:" << prog->log();
        return false;
    }
    return true;
}

void QtGlassFlowScene::initQuad()
{
    if (m_quadInitialized)
        return;
    m_quadVbo.create();
    m_quadVbo.bind();
    m_quadVbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_quadVbo.allocate(kQuadVerts, sizeof(kQuadVerts));
    m_quadVbo.release();
    m_quadInitialized = true;
}

void QtGlassFlowScene::drawFullscreenQuad(QOpenGLShaderProgram *prog)
{
    m_quadVbo.bind();
    const int stride = 4 * sizeof(float);
    prog->enableAttributeArray(0);
    prog->enableAttributeArray(1);
    prog->setAttributeBuffer(0, GL_FLOAT, 0,                 2, stride);
    prog->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, stride);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    prog->disableAttributeArray(0);
    prog->disableAttributeArray(1);
    m_quadVbo.release();
}

void QtGlassFlowScene::initializeGL()
{
    initializeOpenGLFunctions();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Blit shader (compiled from inline source)
    m_blitShader = new QOpenGLShaderProgram(this);
    m_blitShader->addShaderFromSourceCode(QOpenGLShader::Vertex,   kBlitVertSrc);
    m_blitShader->addShaderFromSourceCode(QOpenGLShader::Fragment, kBlitFragSrc);
    m_blitShader->bindAttributeLocation("a_position", 0);
    m_blitShader->bindAttributeLocation("a_texcoord", 1);
    if (!m_blitShader->link())
        qWarning() << "blit shader link failed:" << m_blitShader->log();

    // Blur shader
    m_blurShader = new QOpenGLShaderProgram(this);
    compileProgram(m_blurShader,
                   QStringLiteral(":/qtglassflow/shaders/blur_vertex.glsl"),
                   QStringLiteral(":/qtglassflow/shaders/blur_fragment.glsl"));

    // Glass shader
    m_glassShader = new QOpenGLShaderProgram(this);
    compileProgram(m_glassShader,
                   QStringLiteral(":/qtglassflow/shaders/scene_vertex.glsl"),
                   QStringLiteral(":/qtglassflow/shaders/scene_fragment.glsl"));

    initQuad();

    if (!m_bgPath.isEmpty())
        loadBackgroundTexture();

    // Animation timer
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() { update(); });
    m_timer->start(16);
    m_clock.start();
}

void QtGlassFlowScene::resizeGL(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    createFBOs(w, h);
    glViewport(0, 0, w, h);
}

void QtGlassFlowScene::createFBOs(int w, int h)
{
    destroyFBOs();
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    fmt.setInternalTextureFormat(GL_RGBA8);

    m_sceneFbo    = new QOpenGLFramebufferObject(w, h, fmt);
    m_blurPingFbo = new QOpenGLFramebufferObject(w, h, fmt);
    m_blurPongFbo = new QOpenGLFramebufferObject(w, h, fmt);

    auto setLinear = [this](QOpenGLFramebufferObject *fbo) {
        glBindTexture(GL_TEXTURE_2D, fbo->texture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    setLinear(m_sceneFbo);
    setLinear(m_blurPingFbo);
    setLinear(m_blurPongFbo);
}

void QtGlassFlowScene::destroyFBOs()
{
    delete m_sceneFbo;    m_sceneFbo    = nullptr;
    delete m_blurPingFbo; m_blurPingFbo = nullptr;
    delete m_blurPongFbo; m_blurPongFbo = nullptr;
}

void QtGlassFlowScene::loadBackgroundTexture()
{
    if (m_bgPath.isEmpty())
        return;
    QImage img(m_bgPath);
    if (img.isNull()) {
        qWarning() << "failed to load background image:" << m_bgPath;
        return;
    }
    img = img.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);

    if (m_bgTexture == 0)
        glGenTextures(1, &m_bgTexture);
    glBindTexture(GL_TEXTURE_2D, m_bgTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_bgWidth  = img.width();
    m_bgHeight = img.height();
    m_bgDirty  = false;
}

void QtGlassFlowScene::renderBackgroundToFbo()
{
    if (!m_sceneFbo)
        return;
    m_sceneFbo->bind();
    glViewport(0, 0, m_sceneFbo->width(), m_sceneFbo->height());
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_bgTexture) {
        glDisable(GL_BLEND);
        m_blitShader->bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_bgTexture);
        m_blitShader->setUniformValue("u_texture", 0);
        drawFullscreenQuad(m_blitShader);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_blitShader->release();
    }

    m_sceneFbo->release();
}

void QtGlassFlowScene::runBlurPass()
{
    if (!m_sceneFbo || !m_blurPingFbo || !m_blurPongFbo)
        return;

    m_blurShader->bind();

    const float w = float(m_sceneFbo->width());
    const float h = float(m_sceneFbo->height());
    m_blurShader->setUniformValue("u_resolution", QVector2D(w, h));
    m_blurShader->setUniformValue("u_radius",     m_blurRadius);
    m_blurShader->setUniformValue("u_texture",    0);

    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);

    GLuint srcTex = m_sceneFbo->texture();

    for (int i = 0; i < m_blurIterations; ++i) {
        // Horizontal pass: src -> ping
        m_blurPingFbo->bind();
        glViewport(0, 0, m_blurPingFbo->width(), m_blurPingFbo->height());
        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        m_blurShader->setUniformValue("u_direction", QVector2D(1.0f, 0.0f));
        drawFullscreenQuad(m_blurShader);
        m_blurPingFbo->release();

        // Vertical pass: ping -> pong
        m_blurPongFbo->bind();
        glViewport(0, 0, m_blurPongFbo->width(), m_blurPongFbo->height());
        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, m_blurPingFbo->texture());
        m_blurShader->setUniformValue("u_direction", QVector2D(0.0f, 1.0f));
        drawFullscreenQuad(m_blurShader);
        m_blurPongFbo->release();

        // Subsequent iterations read from pong
        srcTex = m_blurPongFbo->texture();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    m_blurShader->release();
}

void QtGlassFlowScene::blitTextureToScreen(GLuint tex)
{
    glDisable(GL_BLEND);
    m_blitShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    m_blitShader->setUniformValue("u_texture", 0);
    drawFullscreenQuad(m_blitShader);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_blitShader->release();
}

QPointF QtGlassFlowScene::widgetToNdc(const QPointF &p) const
{
    const float w = float(width());
    const float h = float(height());
    if (w <= 0.0f || h <= 0.0f)
        return QPointF(0, 0);
    // Widget Y goes down, NDC Y goes up
    float nx = (p.x() / w) * 2.0f - 1.0f;
    float ny = 1.0f - (p.y() / h) * 2.0f;
    return QPointF(nx, ny);
}

QSizeF QtGlassFlowScene::widgetSizeToNdc(const QSizeF &s) const
{
    const float w = float(width());
    const float h = float(height());
    if (w <= 0.0f || h <= 0.0f)
        return QSizeF(0, 0);
    return QSizeF((s.width() / w) * 2.0f, (s.height() / h) * 2.0f);
}

void QtGlassFlowScene::renderGlassObject(int index)
{
    if (index < 0 || index >= m_objects.size())
        return;
    const GlassObject &obj = m_objects[index];

    // Object center in widget coords -> NDC
    QPointF centerWidget(obj.position.x() + obj.size.width()  * 0.5,
                         obj.position.y() + obj.size.height() * 0.5);
    QPointF centerNdc = widgetToNdc(centerWidget);
    QSizeF  fullNdc   = widgetSizeToNdc(obj.size);
    QSizeF  halfNdc(fullNdc.width() * 0.5, fullNdc.height() * 0.5);

    // Effective power factor (blends per-object value with global override)
    float power = obj.powerFactor > 0.0f ? obj.powerFactor : m_globalPower;

    m_glassShader->bind();
    m_glassShader->setUniformValue("u_blurredTex",  0);
    m_glassShader->setUniformValue("u_resolution",  QVector2D(width(), height()));
    m_glassShader->setUniformValue("u_objCenter",   QVector2D(centerNdc.x(), centerNdc.y()));
    m_glassShader->setUniformValue("u_objHalfSize", QVector2D(halfNdc.width(), halfNdc.height()));
    m_glassShader->setUniformValue("u_powerFactor", power);
    m_glassShader->setUniformValue("u_a",           m_refractionA);
    m_glassShader->setUniformValue("u_b",           m_refractionB);
    m_glassShader->setUniformValue("u_c",           m_refractionC);
    m_glassShader->setUniformValue("u_d",           m_refractionD);
    m_glassShader->setUniformValue("u_fPower",      m_fPower);
    m_glassShader->setUniformValue("u_noise",       m_noiseAmount);
    m_glassShader->setUniformValue("u_time",        float(m_clock.elapsed()) * 0.001f);

    // Default values for new uniforms (backward compat for GlassObjects)
    m_glassShader->setUniformValue("u_tintColor",      QVector3D(0.0f, 0.0f, 0.0f));
    m_glassShader->setUniformValue("u_tintStrength",   0.0f);
    m_glassShader->setUniformValue("u_opacity",        1.0f);
    m_glassShader->setUniformValue("u_rippleTime",     -1.0f);
    m_glassShader->setUniformValue("u_rippleCenter",   QVector2D(0.5f, 0.5f));
    m_glassShader->setUniformValue("u_flowSpeed",      0.0f);
    m_glassShader->setUniformValue("u_deformAmount",   0.0f);

    // Collect connections originating from this object
    QVector<QVector2D> connCenter;
    QVector<QVector2D> connHalf;
    QVector<float>     connPower;
    QVector<float>     connStrength;
    for (const Connection &c : m_connections) {
        if (c.objA != index && c.objB != index)
            continue;
        int other = (c.objA == index) ? c.objB : c.objA;
        if (other < 0 || other >= m_objects.size())
            continue;
        const GlassObject &ob = m_objects[other];
        QPointF cw(ob.position.x() + ob.size.width() * 0.5,
                   ob.position.y() + ob.size.height() * 0.5);
        QPointF cn = widgetToNdc(cw);
        QSizeF  hn = widgetSizeToNdc(ob.size);
        connCenter.append(QVector2D(cn.x(), cn.y()));
        connHalf.append(QVector2D(hn.width() * 0.5f, hn.height() * 0.5f));
        connPower.append(ob.powerFactor > 0.0f ? ob.powerFactor : m_globalPower);
        connStrength.append(c.strength);
        if (connCenter.size() >= 8)
            break;
    }

    int numConn = connCenter.size();
    m_glassShader->setUniformValue("u_numConnections", numConn);
    if (numConn > 0) {
        m_glassShader->setUniformValueArray("u_connCenterB",   connCenter.constData(),  numConn);
        m_glassShader->setUniformValueArray("u_connHalfSizeB", connHalf.constData(),    numConn);
        m_glassShader->setUniformValueArray("u_connPowerB",    connPower.constData(),   numConn, 1);
        m_glassShader->setUniformValueArray("u_connStrength",  connStrength.constData(), numConn, 1);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_blurPongFbo ? m_blurPongFbo->texture() : 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawFullscreenQuad(m_glassShader);

    glBindTexture(GL_TEXTURE_2D, 0);
    m_glassShader->release();
}

void QtGlassFlowScene::updateConnections()
{
    m_connections.clear();
    if (m_attractionDist <= 0.0f)
        return;
    for (int i = 0; i < m_objects.size(); ++i) {
        for (int j = i + 1; j < m_objects.size(); ++j) {
            const GlassObject &a = m_objects[i];
            const GlassObject &b = m_objects[j];
            QPointF ca(a.position.x() + a.size.width() * 0.5,
                       a.position.y() + a.size.height() * 0.5);
            QPointF cb(b.position.x() + b.size.width() * 0.5,
                       b.position.y() + b.size.height() * 0.5);
            float dx = float(ca.x() - cb.x());
            float dy = float(ca.y() - cb.y());
            float dist = std::sqrt(dx * dx + dy * dy);
            // Account for objects' own radii so adjacent shapes connect smoothly
            float ra = float(qMin(a.size.width(), a.size.height()) * 0.5);
            float rb = float(qMin(b.size.width(), b.size.height()) * 0.5);
            float gap = dist - ra - rb;
            if (gap < m_attractionDist) {
                Connection c;
                c.objA = i;
                c.objB = j;
                float t = qBound(0.0f, 1.0f - gap / m_attractionDist, 1.0f);
                c.strength = t;
                m_connections.append(c);
            }
        }
    }
}

void QtGlassFlowScene::paintGL()
{
    if (m_bgDirty)
        loadBackgroundTexture();

    updateConnections();

    // 1) Render background into scene FBO
    renderBackgroundToFbo();

    // 2) Blur scene FBO into pong FBO via ping-pong
    runBlurPass();

    // 3) Composite to default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, width(), height());
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Background original (sharp)
    if (m_sceneFbo)
        blitTextureToScreen(m_sceneFbo->texture());

    // Glass objects
    for (int i = 0; i < m_objects.size(); ++i)
        renderGlassObject(i);

    glDisable(GL_BLEND);

    // 4) Text overlay via QPainter (after GL operations finish)
    bool anyText = false;
    for (const GlassObject &o : m_objects) {
        if (!o.text.isEmpty()) { anyText = true; break; }
    }
    if (anyText) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont font = painter.font();
        font.setPointSizeF(12.0);
        font.setBold(true);
        painter.setFont(font);
        for (int i = 0; i < m_objects.size(); ++i) {
            const GlassObject &o = m_objects[i];
            if (o.text.isEmpty())
                continue;
            QRectF rect(o.position, o.size);
            QColor textColor = (i == m_hoveredIndex) ? QColor(255, 255, 255)
                                                     : QColor(240, 240, 248);
            // Soft shadow for legibility
            painter.setPen(QColor(0, 0, 0, 120));
            painter.drawText(rect.translated(1, 1), Qt::AlignCenter, o.text);
            painter.setPen(textColor);
            painter.drawText(rect, Qt::AlignCenter, o.text);
        }
    }
}

bool QtGlassFlowScene::hitTest(int objIndex, const QPointF &widgetPos) const
{
    if (objIndex < 0 || objIndex >= m_objects.size())
        return false;
    const GlassObject &o = m_objects[objIndex];
    if (o.size.width() <= 0.0 || o.size.height() <= 0.0)
        return false;

    // Local coordinate in [-1,1]
    double cx = o.position.x() + o.size.width()  * 0.5;
    double cy = o.position.y() + o.size.height() * 0.5;
    double lx = (widgetPos.x() - cx) / (o.size.width()  * 0.5);
    double ly = (widgetPos.y() - cy) / (o.size.height() * 0.5);

    double n = double(o.powerFactor > 0.0f ? o.powerFactor : m_globalPower);
    double v = std::pow(std::abs(lx), n) + std::pow(std::abs(ly), n);
    return v <= 1.0;
}

void QtGlassFlowScene::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Topmost-first hit test for GlassObjects
        for (int i = m_objects.size() - 1; i >= 0; --i) {
            if (hitTest(i, event->pos())) {
                m_dragIndex = i;
                m_objects[i].state = Pressed;
                m_objects[i].dragging = true;
                m_objects[i].dragOffset =
                    event->pos() - m_objects[i].position;
                emit objectClicked(i);
                update();
                return;
            }
        }
    }
    QOpenGLWidget::mousePressEvent(event);
}

void QtGlassFlowScene::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragIndex >= 0 && m_dragIndex < m_objects.size()) {
        GlassObject &o = m_objects[m_dragIndex];
        if (o.dragging) {
            QPointF newPos = event->pos() - o.dragOffset;
            // Keep inside the widget
            newPos.setX(qBound<qreal>(0.0, newPos.x(), width()  - o.size.width()));
            newPos.setY(qBound<qreal>(0.0, newPos.y(), height() - o.size.height()));
            o.position = newPos;
            update();
            return;
        }
    }

    // GlassObject hover tracking
    int newHover = -1;
    for (int i = m_objects.size() - 1; i >= 0; --i) {
        if (hitTest(i, event->pos())) {
            newHover = i;
            break;
        }
    }
    if (newHover != m_hoveredIndex) {
        if (m_hoveredIndex >= 0 && m_hoveredIndex < m_objects.size()
            && m_objects[m_hoveredIndex].state != Pressed)
            m_objects[m_hoveredIndex].state = Normal;
        m_hoveredIndex = newHover;
        if (newHover >= 0)
            m_objects[newHover].state = Hovered;
        update();
    }

    QOpenGLWidget::mouseMoveEvent(event);
}

void QtGlassFlowScene::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // GlassObject release
        if (m_dragIndex >= 0 && m_dragIndex < m_objects.size()) {
            GlassObject &o = m_objects[m_dragIndex];
            o.dragging = false;
            o.state = hitTest(m_dragIndex, event->pos()) ? Hovered : Normal;
            m_dragIndex = -1;
            update();
            return;
        }
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void QtGlassFlowScene::leaveEvent(QEvent *event)
{
    if (m_hoveredIndex >= 0 && m_hoveredIndex < m_objects.size()
        && m_objects[m_hoveredIndex].state != Pressed)
        m_objects[m_hoveredIndex].state = Normal;
    m_hoveredIndex = -1;
    update();
    QOpenGLWidget::leaveEvent(event);
}

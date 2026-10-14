#include "SurfaceGeometry.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPlatformSurfaceEvent>
#include <QQuickView>
#include <QQuickItem>
#include <QTimer>
#include <QWindow>
#include <exception>

// Basic Qt native window and event adapter; no Qt graphics renderer or backing store.
class VulkanViewport final : public QWindow
{
  public:
    SurfaceGeometry surface;

  protected:
    void exposeEvent(QExposeEvent *) override
    {
        if (isExposed())
            requestUpdate();
    }
    void resizeEvent(QResizeEvent *) override
    {
        if (isExposed())
            requestUpdate();
    }
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::PlatformSurface)
        {
            auto *platformEvent = static_cast<QPlatformSurfaceEvent *>(event);
            if (platformEvent->surfaceEventType() ==
                QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            {
                // Release Vulkan resources before Qt destroys the native window.
                surface.release();
            }
        }
        else if (event->type() == QEvent::UpdateRequest)
        {
            if (isExposed())
            {
                try
                {
                    surface.setFramebufferSize(int(width() * devicePixelRatio()),
                                               int(height() * devicePixelRatio()));
                    surface.initialize(static_cast<std::uintptr_t>(winId()));
                    surface.requestUpdate();
                    surface.render();
                    if (surface.needsUpdate() && width() > 0 && height() > 0)
                        requestUpdate();
                }
                catch (const std::exception &error)
                {
                    QMessageBox::critical(nullptr, "Vulkan renderer",
                                          QString::fromUtf8(error.what()));
                    QCoreApplication::exit(1);
                }
            }
            return true;
        }
        return QWindow::event(event);
    }
};

// 普通 Qt 控件创建函数：QML 发出信号，标准 QTimer 合并同一轮事件中的更新。
// 不定义 Q_OBJECT/Q_PROPERTY，也不需要项目生成的 moc 文件。
QWidget *createControls(VulkanViewport *viewport, QWidget *parent)
{
    auto *controls = new QQuickView;
    controls->setResizeMode(QQuickView::SizeRootObjectToView);
    controls->setInitialProperties(
        {{"interpolationError", viewport->surface.interpolationError()}});
    controls->setSource(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (controls->status() == QQuickView::Error)
    {
        delete controls;
        return nullptr;
    }

    auto *panel = QWidget::createWindowContainer(controls, parent);
    panel->setFixedWidth(280);
    auto *root = controls->rootObject();
    auto *update = new QTimer(panel);
    update->setSingleShot(true);
    update->setInterval(0);

    auto applyControls = [viewport, root]
    {
        auto &surface = viewport->surface;
        surface.spin = root->property("cameraSpin").toFloat();
        surface.tilt = root->property("cameraTilt").toFloat();
        surface.distance = root->property("cameraDistance").toFloat();
        surface.gridVisible = root->property("showGrid").toBool();
        surface.pointsVisible = root->property("showPoints").toBool();
        surface.controlPointsVisible = root->property("showControlPoints").toBool();
        surface.requestUpdate();
        viewport->requestUpdate();
    };

    // QML 动态信号连接到 Qt 自带的 start() 槽；timeout 再调用普通 C++ lambda。
    if (!QObject::connect(root, SIGNAL(settingsChanged()), update, SLOT(start())))
    {
        delete panel;
        return nullptr;
    }
    QObject::connect(update, &QTimer::timeout, viewport, applyControls);
    applyControls();
    return panel;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (QGuiApplication::platformName() != QStringLiteral("xcb"))
    {
        QMessageBox::critical(nullptr, "Unsupported window system",
                              "This native Vulkan surface implementation uses X11. "
                              "Run with QT_QPA_PLATFORM=xcb.");
        return 1;
    }

    QWidget window;
    window.setWindowTitle("NURBS — Vulkan / QML controls");
    window.resize(1100, 800);

    auto *row = new QHBoxLayout(&window);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    auto *viewport = new VulkanViewport;
    row->addWidget(QWidget::createWindowContainer(viewport), 1);

    auto *panel = createControls(viewport, &window);
    if (!panel)
        return 1;
    row->addWidget(panel);

    window.show();
    return app.exec();
}


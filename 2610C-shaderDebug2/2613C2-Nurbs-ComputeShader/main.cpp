#include "SurfaceGeometry.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QQuickView>
#include <QQuickItem>
#include <QTimer>
#include <exception>
#include <functional>
#include <cmath>
#include <random>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>

// Generate a row-major CPU grid, then upload it through the public input API.
void setSurfaceStyle(SurfaceGeometry &surface, int style)
{
    const int rowCounts[] = {10, 13, 16}, colCounts[] = {11, 9, 12};
    if (style < 0 || style > 2) style = 0;
    const int rows = rowCounts[style], cols = colCounts[style];
    std::mt19937 random(2026u + static_cast<unsigned>(style));
    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    std::vector<SurfaceGeometry::InputPoint> inputPoints(rows * cols);
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
        {
            float x = 2.0f * row / (rows - 1) - 1.0f;
            float y = 2.0f * col / (cols - 1) - 1.0f;
            float z;
            switch (style)
            {
                case 1: // Bowl.
                    z = 0.4f * (x * x + y * y) - 0.4f;
                    break;
                case 2: // Saddle.
                    z = 0.5f * (x * x - y * y);
                    break;
                default: // Waves.
                    z = 0.35f * std::sin(3.0f * x) * std::cos(3.0f * y) + 0.15f * x * y;
                    break;
            }
            x += 0.3f * jitter(random) / (rows - 1);
            y += 0.3f * jitter(random) / (cols - 1);
            z += 0.08f * jitter(random);
            inputPoints[row * cols + col] = {100 * x, 100 * z, 100 * y, 1};
        }
    surface.setInputPoints(inputPoints, rows, cols);
}

// 普通 Qt 控件创建函数：QML 发出信号，标准 QTimer 合并同一轮事件中的更新。
// 不定义 Q_OBJECT/Q_PROPERTY，也不需要项目生成的 moc 文件。
QWidget *createControls(SurfaceGeometry *viewport, QWidget *parent)
{
    auto *controls = new QQuickView;
    controls->setResizeMode(QQuickView::SizeRootObjectToView);
    controls->setSource(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (controls->status() == QQuickView::Error)
    {
        delete controls;
        return nullptr;
    }

    auto *panel = QWidget::createWindowContainer(controls, parent);
    panel->setFixedWidth(280);
    auto *root = controls->rootObject();
    viewport->rotateCamera = [root](qreal spinDelta, qreal tiltDelta)
    {
        QMetaObject::invokeMethod(root, "rotateCamera",
                                  Q_ARG(QVariant, QVariant(spinDelta)),
                                  Q_ARG(QVariant, QVariant(tiltDelta)));
    };
    viewport->panCamera = [root](qreal xDelta, qreal yDelta)
    {
        root->setProperty("cameraPanX", root->property("cameraPanX").toDouble() + xDelta);
        root->setProperty("cameraPanY", root->property("cameraPanY").toDouble() + yDelta);
    };
    viewport->moveCoordinates = [root](qreal dx, qreal dy)
    {
        constexpr qreal radians = 3.14159265358979323846 / 180.0;
        const qreal spin = root->property("cameraSpin").toDouble() * radians;
        const qreal tilt = root->property("cameraTilt").toDouble() * radians;
        // Move the origin in the camera plane, expressed in world coordinates.
        const qreal x = std::cos(spin) * dx + std::sin(spin) * std::sin(tilt) * dy;
        const qreal y = std::cos(tilt) * dy;
        const qreal z = std::sin(spin) * dx - std::cos(spin) * std::sin(tilt) * dy;
        root->setProperty("coordinateX", root->property("coordinateX").toDouble() + x);
        root->setProperty("coordinateY", root->property("coordinateY").toDouble() + y);
        root->setProperty("coordinateZ", root->property("coordinateZ").toDouble() + z);
        // Compensate the pivot change so the surface stays in place.
        root->setProperty("cameraPanX", root->property("cameraPanX").toDouble() + dx);
        root->setProperty("cameraPanY", root->property("cameraPanY").toDouble() + dy);
    };
    viewport->moveAxisDepth = [root](qreal depthDelta)
    {
        const qreal spin = root->property("cameraSpin").toDouble();
        const qreal tilt = root->property("cameraTilt").toDouble();
        const glm::dquat quatX = glm::angleAxis(glm::radians(-tilt), glm::dvec3(1, 0, 0));
        const glm::dquat quatY = glm::angleAxis(glm::radians(-spin), glm::dvec3(0, 1, 0));
        const glm::dvec3 viewZ = quatY * quatX * glm::dvec3(0, 0, 1);
        const glm::dvec3 translation = depthDelta * viewZ;
        root->setProperty("coordinateX", root->property("coordinateX").toDouble() + translation.x);
        root->setProperty("coordinateY", root->property("coordinateY").toDouble() + translation.y);
        root->setProperty("coordinateZ", root->property("coordinateZ").toDouble() + translation.z);
        // Cancel the scene's pivot-depth translation, preserving surface depth
        // while allowing the axis origin itself to move nearer/farther.
        root->setProperty("cameraPanZ", root->property("cameraPanZ").toDouble() + depthDelta);
    };
    viewport->zoomCamera = [root, viewport](qreal steps, QPointF position)
    {
        if (viewport->height() <= 0)
            return;

        const qreal oldDistance = root->property("cameraDistance").toDouble();
        QMetaObject::invokeMethod(root, "zoomCamera", Q_ARG(QVariant, QVariant(steps)));
        const qreal newDistance = root->property("cameraDistance").toDouble();
        // Preserve the camera-plane point under the cursor, using the actual
        // clamped zoom distance and logical pixels (also correct on HiDPI).
        const qreal scaleDelta = 2.0 * (newDistance - oldDistance) *
                                 std::tan(3.14159265358979323846 / 8.0) / viewport->height();
        const qreal dx = (position.x() - viewport->width() / 2.0) * scaleDelta;
        const qreal dy = (viewport->height() / 2.0 - position.y()) * scaleDelta;
        root->setProperty("cameraPanX", root->property("cameraPanX").toDouble() + dx);
        root->setProperty("cameraPanY", root->property("cameraPanY").toDouble() + dy);
    };
    QObject::connect(root, &QObject::destroyed, viewport, [viewport]()
    {
        viewport->rotateCamera = {};
        viewport->panCamera = {};
        viewport->moveCoordinates = {};
        viewport->moveAxisDepth = {};
        viewport->zoomCamera = {};
        viewport->pickRotationCenter = {};
    });
    auto *update = new QTimer(panel);
    update->setSingleShot(true);
    update->setInterval(0);

    auto applyControls = [viewport, root, lastStyle = 0]() mutable
    {
        auto &surface = *viewport;
        const int style = root->property("surfaceStyle").toInt();
        if (style != lastStyle)
        {
            setSurfaceStyle(surface, style);
            lastStyle = style;
        }
        viewport->spin = root->property("cameraSpin").toFloat();
        viewport->tilt = root->property("cameraTilt").toFloat();
        viewport->distance = root->property("cameraDistance").toFloat();
        viewport->panX = root->property("cameraPanX").toFloat();
        viewport->panY = root->property("cameraPanY").toFloat();
        viewport->panZ = root->property("cameraPanZ").toFloat();
        viewport->coordinate = {root->property("coordinateX").toFloat(),
                              root->property("coordinateY").toFloat(),
                              root->property("coordinateZ").toFloat()};
        surface.gridVisible = root->property("showGrid").toBool();
        surface.pointsVisible = root->property("showPoints").toBool();
        surface.controlPointsVisible = root->property("showControlPoints").toBool();
        viewport->requestUpdate();
    };

    // QML 动态信号连接到 Qt 自带的 start() 槽；timeout 再调用普通 C++ lambda。
    if (!QObject::connect(root, SIGNAL(settingsChanged()), update, SLOT(start())))
    {
        delete panel;
        return nullptr;
    }
    QObject::connect(update, &QTimer::timeout, viewport, applyControls);
    viewport->pickRotationCenter = [viewport, root, update, applyControls](QPointF position) mutable
    {
        try
        {
            update->stop();
            applyControls();
            viewport->render();
            std::array<float, 3> point;
            if (!viewport->pickPosition(int(position.x() * viewport->devicePixelRatio()),
                                                int(position.y() * viewport->devicePixelRatio()), point))
                return;
            constexpr qreal radians = 3.14159265358979323846 / 180.0;
            const qreal spin = root->property("cameraSpin").toDouble() * radians;
            const qreal tilt = root->property("cameraTilt").toDouble() * radians;
            const qreal dx = point[0] - root->property("coordinateX").toDouble();
            const qreal dy = point[1] - root->property("coordinateY").toDouble();
            const qreal dz = point[2] - root->property("coordinateZ").toDouble();
            // Compensate the pivot change in camera space to preserve the view.
            root->setProperty("cameraPanX", root->property("cameraPanX").toDouble() +
                              std::cos(spin) * dx + std::sin(spin) * dz);
            root->setProperty("cameraPanY", root->property("cameraPanY").toDouble() +
                              std::sin(tilt) * std::sin(spin) * dx + std::cos(tilt) * dy -
                              std::sin(tilt) * std::cos(spin) * dz);
            root->setProperty("cameraPanZ", root->property("cameraPanZ").toDouble() -
                              std::cos(tilt) * std::sin(spin) * dx + std::sin(tilt) * dy +
                              std::cos(tilt) * std::cos(spin) * dz);
            root->setProperty("coordinateX", point[0]);
            root->setProperty("coordinateY", point[1]);
            root->setProperty("coordinateZ", point[2]);
        }
        catch (const std::exception &error)
        {
            QMessageBox::critical(nullptr, "Depth picking", QString::fromUtf8(error.what()));
        }
    };
    applyControls();
    return panel;
}

int main(int argc, char **argv)
{
    std::vector<glm::vec4> positionStorage(42766);

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

    auto *viewport = new SurfaceGeometry;

    setSurfaceStyle(*viewport, 0);

    auto *viewportContainer = QWidget::createWindowContainer(viewport);

    auto *panel = createControls(viewport, &window);
    if (!panel)
        return 1;
    row->addWidget(panel);
    row->addWidget(viewportContainer, 1);

    window.show();
    return app.exec();
}


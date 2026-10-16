#include "SurfaceGeometry.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QQuickView>
#include <QQuickItem>
#include <QTimer>
#include <cmath>
#include <random>
#include <glm/vec4.hpp>

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
    viewport->setNavigationControls(root);
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
        viewport->applyCameraControls();
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

    viewport->prepareNavigation = [update, applyControls]() mutable
    {
        update->stop();
        applyControls();
    };
    QObject::connect(root, &QObject::destroyed, viewport, [viewport]()
    {
        viewport->prepareNavigation = {};
    });
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


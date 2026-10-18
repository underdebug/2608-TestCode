#include "VolumeRendering.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QQuickView>
#include <QQuickItem>
#include <QTimer>
#include <cstdio>
#include <exception>

// 普通 Qt 控件创建函数：QML 发出信号，标准 QTimer 合并同一轮事件中的更新。
// 不定义 Q_OBJECT/Q_PROPERTY，也不需要项目生成的 moc 文件。
QWidget *createControls(VolumeRendering *viewport, QWidget *parent)
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

    auto applyControls = [viewport, root]()
    {
        viewport->applyCameraControls();
        viewport->windowCenter = root->property("windowCenter").toFloat();
        viewport->windowWidth = root->property("windowWidth").toFloat();
        viewport->opacity = root->property("volumeOpacity").toFloat();
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
    const std::string directory = argc > 1 ? argv[1] : "/home/roots/develop/data/DICOM";
    const bool validate = argc > 2 && std::string(argv[2]) == "--validate";
    if (validate)
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    if (validate)
    {
        try { VolumeRendering volume; volume.loadDicom(directory); return 0; }
        catch (const std::exception &error) { fprintf(stderr, "%s\n", error.what()); return 1; }
    }

    if (QGuiApplication::platformName() != QStringLiteral("xcb"))
    {
        QMessageBox::critical(nullptr, "Unsupported window system",
                              "This native Vulkan surface implementation uses X11. "
                              "Run with QT_QPA_PLATFORM=xcb.");
        return 1;
    }

    QWidget window;
    window.setWindowTitle("DICOM volume — Vulkan");
    window.resize(1100, 800);

    auto *row = new QHBoxLayout(&window);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    auto *viewport = new VolumeRendering;

    try { viewport->loadDicom(directory); }
    catch (const std::exception &error)
    {
        QMessageBox::critical(nullptr, "DICOM loading failed", error.what());
        delete viewport;
        return 1;
    }

    auto *viewportContainer = QWidget::createWindowContainer(viewport);

    auto *panel = createControls(viewport, &window);
    if (!panel)
        return 1;
    row->addWidget(panel);
    row->addWidget(viewportContainer, 1);

    window.show();
    return app.exec();
}


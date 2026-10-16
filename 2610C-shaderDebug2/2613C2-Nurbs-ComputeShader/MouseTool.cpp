#include "MouseTool.h"
#include "VulkanWindow.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <cstdio>

MousePan::MousePan(VulkanWindow &window) : window(window)
{
}

bool MousePan::move(QMouseEvent &event, QPointF delta)
{
    const bool shift = event.modifiers().testFlag(Qt::ShiftModifier);
    if ((!shift && event.modifiers().testFlag(Qt::ControlModifier)) ||
        (!shift && !event.buttons().testFlag(Qt::LeftButton)))
        return false;
    if (window.panCamera && window.height() > 0)
    {
        const auto ortho = window.orthographicBounds();
        const qreal scale = (ortho[3] - ortho[2]) / window.height();
        window.panCamera(delta.x() * scale, -delta.y() * scale);
    }
    event.accept();
    return true;
}

MouseRotate::MouseRotate(VulkanWindow &window) : window(window)
{
}

bool MouseRotate::move(QMouseEvent &event, QPointF delta)
{
    if (event.modifiers().testFlag(Qt::ShiftModifier) ||
        (!event.modifiers().testFlag(Qt::ControlModifier) && !event.buttons().testFlag(Qt::RightButton)))
        return false;
    if (window.rotateCamera)
        window.rotateCamera(delta.x() * 0.5, delta.y() * 0.5);
    event.accept();
    return true;
}

MouseZoom::MouseZoom(VulkanWindow &window) : window(window)
{
}

bool MouseZoom::wheel(QWheelEvent &event)
{
    const qreal steps = !event.pixelDelta().isNull()
        ? event.pixelDelta().y() / 120.0 : event.angleDelta().y() / 120.0;
    if (!window.zoomCamera || steps == 0)
        return false;
    window.zoomCamera(steps, event.position());
    event.accept();
    return true;
}

MouseAxis::MouseAxis(VulkanWindow &window) : window(window)
{
}

bool MouseAxis::wheel(QWheelEvent &event)
{
    if (!event.modifiers().testFlag(Qt::AltModifier))
        return false;
    const QPoint angle = event.angleDelta();
    const QPoint pixel = event.pixelDelta();
    // Some input paths report horizontal deltas with Alt held, or pixel-only scrolling.
    const int angleOffset = angle.y() != 0 ? angle.y() : angle.x();
    const int pixelOffset = pixel.y() != 0 ? pixel.y() : pixel.x();
    const float offset = static_cast<float>(angleOffset != 0 ? angleOffset : pixelOffset);

    if (window.moveAxisDepth && offset != 0 && window.height() > 0)
    {
        const auto ortho = window.orthographicBounds();
        const qreal orthoHeight = fabs(ortho[3] - ortho[2]);
        const qreal transZ = offset * orthoHeight / window.height() * 0.05;
        window.moveAxisDepth(transZ);
    }
    event.accept();
    return true;
}

bool MouseAxis::press(QMouseEvent &event)
{
    if (event.type() != QEvent::MouseButtonDblClick ||
        !event.modifiers().testFlag(Qt::AltModifier) || event.button() != Qt::LeftButton)
        return false;
    if (window.pickRotationCenter)
        window.pickRotationCenter(event.position());
    event.accept();
    return true;
}

bool MouseAxis::move(QMouseEvent &event, QPointF delta)
{
    const bool alt = event.modifiers().testFlag(Qt::AltModifier);
    const bool meta = event.modifiers().testFlag(Qt::MetaModifier);
    if (!alt && !meta)
        return false;
    // Alt + left-drag shares the same origin movement as Alt hover.
    if (alt && event.buttons().testFlag(Qt::RightButton) &&
        !event.buttons().testFlag(Qt::LeftButton))
    {
        event.accept();
        return true;
    }
    if (event.modifiers().testFlag(Qt::ShiftModifier) ||
        event.modifiers().testFlag(Qt::ControlModifier))
        return false;
    if (window.moveCoordinates && window.height() > 0)
    {
        const auto ortho = window.orthographicBounds();
        const qreal scale = (ortho[3] - ortho[2]) / window.height();
        window.moveCoordinates(delta.x() * scale, -delta.y() * scale);
    }
    event.accept();
    return true;
}

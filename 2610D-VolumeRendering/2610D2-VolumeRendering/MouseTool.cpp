#include "MouseTool.h"
#include "VulkanWindow.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMessageBox>
#include <algorithm>
#include <cmath>
#include <exception>
#include <glm/gtc/quaternion.hpp>

MousePan::MousePan(VulkanWindow &window) : window(window)
{
}

bool MousePan::move(QMouseEvent &event, QPointF delta)
{
    const bool shift = event.modifiers().testFlag(Qt::ShiftModifier);
    if ((!shift && event.modifiers().testFlag(Qt::ControlModifier)) ||
        (!shift && !event.buttons().testFlag(Qt::LeftButton)))
        return false;
    if (window.height() > 0)
    {
        const auto ortho = window.orthographicBounds();
        const qreal scale = (ortho[3] - ortho[2]) / window.height();
        const qreal xDelta = delta.x() * scale, yDelta = -delta.y() * scale;
        window.panX += xDelta;
        window.panY += yDelta;
    }
    window.navigationChanged();
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
    const qreal spinDelta = delta.x() * 0.5, tiltDelta = delta.y() * 0.5;
    window.spin = std::fmod(std::fmod(window.spin + spinDelta + 180, 360) + 360, 360) - 180;
    window.tilt = std::clamp(qreal(window.tilt) + tiltDelta, -90.0, 90.0);
    window.navigationChanged();
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
    if (steps == 0 || window.height() <= 0)
        return false;
    const QPointF position = event.position();
    const qreal oldDistance = window.distance;
    window.distance = std::clamp(oldDistance * std::pow(0.85, steps), 20.0, 5000.0);
    const qreal newDistance = window.distance;
    // Preserve the camera-plane point under the cursor, using the actual
    // clamped zoom distance and logical pixels (also correct on HiDPI).
    const qreal scaleDelta = 2.0 * (newDistance - oldDistance) *
                             std::tan(3.14159265358979323846 / 8.0) / window.height();
    const qreal dx = (position.x() - window.width() / 2.0) * scaleDelta;
    const qreal dy = (window.height() / 2.0 - position.y()) * scaleDelta;
    window.panX += dx;
    window.panY += dy;
    window.navigationChanged();
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

    if (offset != 0 && window.height() > 0)
    {
        const auto ortho = window.orthographicBounds();
        const qreal orthoHeight = ortho[3] - ortho[2];
        const qreal transZ = offset * orthoHeight / window.height() * 0.05;
        const qreal depthDelta = transZ;
        const qreal spin = window.spin;
        const qreal tilt = window.tilt;
        const glm::dquat quatX = glm::angleAxis(glm::radians(-tilt), glm::dvec3(1, 0, 0));
        const glm::dquat quatY = glm::angleAxis(glm::radians(-spin), glm::dvec3(0, 1, 0));
        const glm::dvec3 viewZ = quatY * quatX * glm::dvec3(0, 0, 1);
        const glm::dvec3 translation = depthDelta * viewZ;
        window.coordinate[0] += translation.x;
        window.coordinate[1] += translation.y;
        window.coordinate[2] += translation.z;
        // Cancel the scene's pivot-depth translation, preserving surface depth
        // while allowing the axis origin itself to move nearer/farther.
        window.panZ += depthDelta;
    }
    window.navigationChanged();
    event.accept();
    return true;
}

bool MouseAxis::press(QMouseEvent &event)
{
    if (event.type() != QEvent::MouseButtonDblClick ||
        !event.modifiers().testFlag(Qt::AltModifier) || event.button() != Qt::LeftButton)
        return false;
    const QPointF position = event.position();
    try
    {
        window.render();
        std::array<float, 3> point;
        if (!window.pickPosition(int(position.x() * window.devicePixelRatio()),
                                 int(position.y() * window.devicePixelRatio()), point))
        {
            event.accept();
            return true;
        }
        constexpr qreal radians = 3.14159265358979323846 / 180.0;
        const qreal spin = window.spin * radians;
        const qreal tilt = window.tilt * radians;
        const qreal dx = point[0] - window.coordinate[0];
        const qreal dy = point[1] - window.coordinate[1];
        const qreal dz = point[2] - window.coordinate[2];
        // Compensate the pivot change in camera space to preserve the view.
        window.panX +=
                          std::cos(spin) * dx + std::sin(spin) * dz;
        window.panY +=
                          std::sin(tilt) * std::sin(spin) * dx + std::cos(tilt) * dy -
                          std::sin(tilt) * std::cos(spin) * dz;
        window.panZ -=
                          std::cos(tilt) * std::sin(spin) * dx + std::sin(tilt) * dy +
                          std::cos(tilt) * std::cos(spin) * dz;
        window.coordinate[0] = point[0];
        window.coordinate[1] = point[1];
        window.coordinate[2] = point[2];
    }
    catch (const std::exception &error)
    {
        QMessageBox::critical(nullptr, "Depth picking", QString::fromUtf8(error.what()));
    }
    window.navigationChanged();
    event.accept();
    return true;
}

bool MouseAxis::move(QMouseEvent &event, QPointF delta)
{
    const bool alt = event.modifiers().testFlag(Qt::AltModifier);
    const bool meta = event.modifiers().testFlag(Qt::MetaModifier);
    if (!alt && !meta)
        return false;
    // Alt hover, left-drag and right-drag all move the origin in the view plane.
    if (event.modifiers().testFlag(Qt::ShiftModifier) ||
        event.modifiers().testFlag(Qt::ControlModifier))
        return false;
    if (window.height() > 0)
    {
        const auto ortho = window.orthographicBounds();
        const qreal scale = (ortho[3] - ortho[2]) / window.height();
        const qreal dx = delta.x() * scale, dy = -delta.y() * scale;
        constexpr qreal radians = 3.14159265358979323846 / 180.0;
        const qreal spin = window.spin * radians;
        const qreal tilt = window.tilt * radians;
        // Move the origin in the camera plane, expressed in world coordinates.
        const qreal x = std::cos(spin) * dx + std::sin(spin) * std::sin(tilt) * dy;
        const qreal y = std::cos(tilt) * dy;
        const qreal z = std::sin(spin) * dx - std::cos(spin) * std::sin(tilt) * dy;
        window.coordinate[0] += x;
        window.coordinate[1] += y;
        window.coordinate[2] += z;
        // Compensate the pivot change so the surface stays in place.
        window.panX += dx;
        window.panY += dy;
    }
    window.navigationChanged();
    event.accept();
    return true;
}

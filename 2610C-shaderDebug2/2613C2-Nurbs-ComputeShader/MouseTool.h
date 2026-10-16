#pragma once
#include <QPointF>

class VulkanWindow;
class QMouseEvent;
class QWheelEvent;

// Pan: left-drag or Shift + mouse movement changes panX/panY.
class MousePan
{
  public:
    explicit MousePan(VulkanWindow &window);
    bool move(QMouseEvent &event, QPointF delta);

  private:
    VulkanWindow &window;
};

// Rotate: right-drag or Ctrl + mouse movement changes spin/tilt in degrees.
// Shift takes priority for panning; axis gestures are handled first.
class MouseRotate
{
  public:
    explicit MouseRotate(VulkanWindow &window);
    bool move(QMouseEvent &event, QPointF delta);

  private:
    VulkanWindow &window;
};

// Zoom: wheel / trackpad scrolling changes distance (view scale), adjusting
// panX/panY to keep the point under the cursor fixed. Alt + wheel goes to MouseAxis.
class MouseZoom
{
  public:
    explicit MouseZoom(VulkanWindow &window);
    bool wheel(QWheelEvent &event);

  private:
    VulkanWindow &window;
};

// Move the world-space axis origin / rotation pivot while preserving the surface view.
// Alt or Meta + mouse movement: move in the view plane (without Shift/Ctrl).
// Alt + left-drag and Alt + right-drag have the same behavior as Alt hover.
// Alt + wheel: move in depth. Alt + left double-click: pick a new pivot.
// Pan compensation keeps the surface in place as the origin changes.
class MouseAxis
{
  public:
    explicit MouseAxis(VulkanWindow &window);
    bool press(QMouseEvent &event);
    bool move(QMouseEvent &event, QPointF delta);
    bool wheel(QWheelEvent &event);

  private:
    VulkanWindow &window;
};

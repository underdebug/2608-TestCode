#pragma once
#include <QPointF>

class VulkanWindow;
class QMouseEvent;
class QWheelEvent;

class MousePan
{
  public:
    explicit MousePan(VulkanWindow &window);
    bool move(QMouseEvent &event, QPointF delta);

  private:
    VulkanWindow &window;
};

class MouseRotate
{
  public:
    explicit MouseRotate(VulkanWindow &window);
    bool move(QMouseEvent &event, QPointF delta);

  private:
    VulkanWindow &window;
};

class MouseZoom
{
  public:
    explicit MouseZoom(VulkanWindow &window);
    bool wheel(QWheelEvent &event);

  private:
    VulkanWindow &window;
};

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

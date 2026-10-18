#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include "MouseTool.h"
#include <QWindow>
#include <memory>

// Native Vulkan window: device, swapchain, camera, navigation, picking and rulers.
class VulkanWindow : public QWindow
{
  public:
    explicit VulkanWindow(QWindow *parent = nullptr);
    ~VulkanWindow() override;

    // Rotation in degrees: horizontal spin and vertical tilt (MouseRotate).
    float spin = 25, tilt = 55;

    // Zoom / view scale (MouseZoom): smaller distance makes the surface larger.
    float distance = 450;

    // Camera-space translation in world units: X/Y pan across the view (MousePan).
    // Z compensates origin-depth changes (MouseAxis); it does not control zoom.
    float panX = 0, panY = 0, panZ = 0;
    
    // World-space axis origin / rotation pivot (MouseAxis).
    std::array<float, 3> coordinate{0, 0, 0};

    // Called after mouse tools change the stored camera or origin.
    virtual void navigationChanged() { requestUpdate(); }

    void render();
    // Left, right, bottom, top in camera-plane world units.
    std::array<qreal, 4> orthographicBounds() const;
    // Derived destructors call release() before destroying their GPU resources.
    void release();
    // Background clicks keep their X/Y and borrow the nearest valid depth.
    bool pickPosition(int x, int y, std::array<float, 3> &position);

  protected:
    // Called with a live device; VulkanWindow waits for GPU work before updates/cleanup.
    virtual void initializeScene(VkPhysicalDevice, VkDevice)
    {
    }
    virtual void releaseScene()
    {
    }
    virtual void createScenePipeline(VkRenderPass)
    {
    }
    virtual void releaseScenePipeline()
    {
    }
    virtual void calculateScene(VkCommandBuffer, VkQueue, VkFence)
    {
    }
    virtual void recordScene(VkCommandBuffer, const std::array<float, 16> &, VkExtent2D)
    {
    }

    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

  private:
    MousePan pan;
    MouseRotate rotate;
    MouseZoom zoom;
    MouseAxis axis;
    QPointF lastMousePosition;
    bool hasMousePosition = false;
    int m_width = 0, m_height = 0;
    class Renderer;
    std::unique_ptr<Renderer> m_renderer;
};

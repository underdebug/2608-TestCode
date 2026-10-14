#pragma once
#include <cstdint>
#include <memory>

// Plain Vulkan renderer. The UI passes a native X11 window ID and pixel dimensions.
class SurfaceGeometry
{
  public:
    SurfaceGeometry();
    ~SurfaceGeometry();
    SurfaceGeometry(const SurfaceGeometry &) = delete;
    SurfaceGeometry &operator=(const SurfaceGeometry &) = delete;
    void initialize(std::uintptr_t nativeWindow);
    void release();
    void setFramebufferSize(int width, int height);
    void render();
    void requestUpdate();
    bool needsUpdate() const
    {
        return m_updateRequested;
    }
    float spin = 25, tilt = 55, distance = 450;
    bool gridVisible = true, pointsVisible = true, controlPointsVisible = true;
    double interpolationError() const;

  private:
    class Renderer;
    std::unique_ptr<Renderer> m_renderer;
    int m_width = 0, m_height = 0;
    bool m_updateRequested = true;
};

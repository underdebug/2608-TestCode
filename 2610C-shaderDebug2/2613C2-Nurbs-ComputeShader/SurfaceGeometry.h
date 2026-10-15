#pragma once
#include <cstdint>
#include <array>
#include <vector>
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
    using InputPoint = std::array<float, 4>; // x, y, z, w (currently w must be 1).
    // Row-major: points[row * columns + column]; cubic grids support 4..16 per axis.
    void setInputPoints(const std::vector<InputPoint> &points, int rows, int columns);
    void requestUpdate();
    bool needsUpdate() const
    {
        return m_updateRequested;
    }
    float spin = 25, tilt = 55, distance = 450;
    bool gridVisible = true, pointsVisible = true, controlPointsVisible = true;

  private:
    class Renderer;
    std::unique_ptr<Renderer> m_renderer;
    std::vector<InputPoint> m_inputPoints;
    int m_rows = 10, m_cols = 10;
    bool m_inputDirty = true;
    int m_width = 0, m_height = 0;
    bool m_updateRequested = true;
};

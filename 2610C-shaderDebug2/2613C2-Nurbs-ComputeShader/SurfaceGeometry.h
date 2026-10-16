#pragma once
#include "VulkanWindow.h"
#include <array>
#include <vector>
#include <QPointer>
#include <functional>

// NURBS input data, compute interpolation, GPU buffers and surface shader drawing.
class SurfaceGeometry : public VulkanWindow
{
  public:
    explicit SurfaceGeometry(QWindow *parent = nullptr);
    ~SurfaceGeometry() override;
    SurfaceGeometry(const SurfaceGeometry &) = delete;
    SurfaceGeometry &operator=(const SurfaceGeometry &) = delete;
    using InputPoint = std::array<float, 4>; // x, y, z, w (currently w must be 1).
    // Row-major: points[row * columns + column]; cubic grids support 4..16 per axis.
    void setInputPoints(const std::vector<InputPoint> &points, int rows, int columns);
    void setNavigationControls(QObject *controls);
    void applyCameraControls();
    void navigationChanged() override;
    std::function<void()> prepareNavigation;

    bool gridVisible = true, pointsVisible = true, controlPointsVisible = true;

  protected:
    bool event(QEvent *event) override;
    void initializeScene(VkPhysicalDevice physical, VkDevice device) override;
    void releaseScene() override;
    void createScenePipeline(VkRenderPass renderPass) override;
    void releaseScenePipeline() override;
    void calculateScene(VkCommandBuffer command, VkQueue queue, VkFence fence) override;
    void recordScene(VkCommandBuffer command, const std::array<float, 16> &matrix, VkExtent2D extent) override;

  private:
    QPointer<QObject> navigationControls;
    void initResources();
    void initDescriptors();
    void initPipelineLayout();
    void initComputePipeline();
    void releaseResources();

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    // CPU uploads inputs; compute owns cached display positions.
    VkBuffer inputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory inputMemory = VK_NULL_HANDLE;
    VkBuffer displayBuffer = VK_NULL_HANDLE;
    VkDeviceMemory displayMemory = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    // These counts match the storage layout in nurbs.comp and nurbs.vert.
    static constexpr uint32_t inputPointCount = 16 * 16;
    static constexpr uint32_t gridVertexCount = 40400;
    static constexpr uint32_t markerVertexCount = 6 * inputPointCount;
    static constexpr uint32_t displayPointCount = gridVertexCount + 2 * markerVertexCount;
    static constexpr uint32_t computeGroupSize = 64;
    static constexpr VkDeviceSize pointSize = 4 * sizeof(float);
    static constexpr VkDeviceSize inputBufferSize = inputPointCount * pointSize;
    static constexpr VkDeviceSize displayBufferSize =
        (displayPointCount + 2 * inputPointCount) * pointSize;

    struct CalculationConstants
    {
        int phase, rows, cols;
    };
    static_assert(sizeof(CalculationConstants) == 12);

    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    // Surface pipeline layout and depth-writing graphics pipeline.
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    std::vector<InputPoint> m_inputPoints;
    int m_rows = 10, m_cols = 10;
    bool m_inputDirty = true;
};

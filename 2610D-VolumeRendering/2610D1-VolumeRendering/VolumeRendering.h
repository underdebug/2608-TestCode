#pragma once
#include "VulkanWindow.h"
#include <QPointer>
#include <functional>
#include <vector>
#include <string>
class VolumeRendering : public VulkanWindow
{
public:
    explicit VolumeRendering(QWindow *parent = nullptr);
    ~VolumeRendering() override;
    void loadDicom(const std::string &directory);
    void setNavigationControls(QObject *controls);
    void applyCameraControls();
    void navigationChanged() override;
    std::function<void()> prepareNavigation;
    float windowCenter = 400, windowWidth = 1800, opacity = 1;
protected:
    bool event(QEvent *event) override;
    void initializeScene(VkPhysicalDevice physical, VkDevice device) override;
    void releaseScene() override;
    void createScenePipeline(VkRenderPass renderPass) override;
    void releaseScenePipeline() override;
    void recordScene(VkCommandBuffer command, const std::array<float, 16> &matrix, VkExtent2D extent) override;
private:
    QPointer<QObject> navigationControls;
    std::vector<uint32_t> voxels;
    std::array<int, 4> dimensions{};
    std::array<float, 4> halfSize{};
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer inputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory inputMemory = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
};

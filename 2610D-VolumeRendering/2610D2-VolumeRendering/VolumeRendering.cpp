#include "VolumeRendering.h"
#include <QEvent>
#include <QVariant>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gdcmImageReader.h>
#include <gdcmIPPSorter.h>
#include <gdcmStringFilter.h>
using Matrix4 = std::array<float, 16>;
struct ViewConstants
{
    Matrix4 inverse;
    std::array<float, 4> halfSize;
    std::array<int, 4> dimensions;
    std::array<float, 4> transfer;
};
static_assert(sizeof(ViewConstants) == 112);
static void check(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: VkResult " + std::to_string(result));
}

[[noreturn]] static void fail(const char *message)
{
    throw std::runtime_error(message);
}

static uint32_t memoryType(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    fail("No suitable Vulkan memory type");
}

static void createStorageBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size, VkBuffer &target, VkDeviceMemory &allocation,
                         VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
{
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    check(vkCreateBuffer(device, &info, nullptr, &target), "Create storage buffer");

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, target, &requirements);
    VkMemoryAllocateInfo memoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memoryInfo.allocationSize = requirements.size;
    memoryInfo.memoryTypeIndex = memoryType(physical, requirements.memoryTypeBits, flags);
    check(vkAllocateMemory(device, &memoryInfo, nullptr, &allocation), "Allocate storage buffer");
    check(vkBindBufferMemory(device, target, allocation, 0), "Bind storage buffer");
}

static VkShaderModule shader(VkDevice device, const char *name)
{
    const auto directory = std::filesystem::read_symlink("/proc/self/exe").parent_path();
    const auto path = directory / "shader" / name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open shader: " + path.string());

    const auto size = file.tellg();
    if (size <= 0 || size % sizeof(uint32_t) != 0)
        throw std::runtime_error("Invalid SPIR-V size: " + path.string());

    std::vector<uint32_t> words(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Cannot read shader: " + path.string());
    if (words.front() != 0x07230203u)
        throw std::runtime_error("Invalid SPIR-V header: " + path.string());

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * sizeof(uint32_t);
    ci.pCode = words.data();
    VkShaderModule module;
    check(vkCreateShaderModule(device, &ci, nullptr, &module), "Create shader");
    return module;
}

static VkPipeline volumePipeline(VkDevice device, VkRenderPass renderPass,
                               VkPipelineLayout pipelineLayout, const char *vertexShader, bool depthWrite)
{
    // The fullscreen triangle supplies normalized screen coordinates.
    VkShaderModule vert = shader(device, vertexShader), frag = shader(device, "volume.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i)
    {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;

    // Generate a fullscreen triangle without vertex attributes.
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1;
    rs.cullMode = VK_CULL_MODE_NONE;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The fragment shader supplies the volume picking depth.
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    // 组合各阶段配置；管线与当前 renderPass 的附件格式匹配。
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dynamic;
    ci.layout = pipelineLayout;
    ci.renderPass = renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    check(result, "Create volume pipeline");
    return pipeline;
}

void VolumeRendering::setNavigationControls(QObject *controls)
{
    navigationControls = controls;
}

void VolumeRendering::applyCameraControls()
{
    auto *root = navigationControls.data();
    if (!root)
        return;
    spin = root->property("cameraSpin").toFloat();
    tilt = root->property("cameraTilt").toFloat();
    distance = root->property("cameraDistance").toFloat();
    panX = root->property("cameraPanX").toFloat();
    panY = root->property("cameraPanY").toFloat();
    panZ = root->property("cameraPanZ").toFloat();
    coordinate[0] = root->property("coordinateX").toFloat();
    coordinate[1] = root->property("coordinateY").toFloat();
    coordinate[2] = root->property("coordinateZ").toFloat();
}

void VolumeRendering::navigationChanged()
{
    if (auto *root = navigationControls.data())
    {
        QMetaObject::invokeMethod(root, "setCamera", Q_ARG(QVariant, QVariant(spin)),
                                  Q_ARG(QVariant, QVariant(tilt)),
                                  Q_ARG(QVariant, QVariant(distance)));
        root->setProperty("cameraPanX", panX);
        root->setProperty("cameraPanY", panY);
        root->setProperty("cameraPanZ", panZ);
        root->setProperty("coordinateX", coordinate[0]);
        root->setProperty("coordinateY", coordinate[1]);
        root->setProperty("coordinateZ", coordinate[2]);
    }
    VulkanWindow::navigationChanged();
}

bool VolumeRendering::event(QEvent *event)
{
    // Apply queued panel edits before mouse tools read or modify camera data.
    if (prepareNavigation && (event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::Wheel))
        prepareNavigation();
    return VulkanWindow::event(event);
}


VolumeRendering::VolumeRendering(QWindow *parent) : VulkanWindow(parent) {}
VolumeRendering::~VolumeRendering()
{
    VulkanWindow::release();
    releaseScene();
}
void VolumeRendering::loadDicom(const std::string &directory)
{
    if (device) throw std::runtime_error("Load DICOM before opening the viewport");
    std::vector<std::string> files;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file()) files.push_back(entry.path().string());
    gdcm::IPPSorter sorter;
    sorter.SetComputeZSpacing(true);
    if (!sorter.Sort(files) || sorter.GetFilenames().empty())
        throw std::runtime_error("Cannot spatially sort DICOM series; use a directory containing one series");
    files = sorter.GetFilenames();
    std::string series;
    std::vector<uint16_t> samples;
    double spacing[3]{};
    for (const auto &file : files)
    {
        gdcm::ImageReader reader;
        reader.SetFileName(file.c_str());
        if (!reader.Read()) throw std::runtime_error("Cannot decode DICOM: " + file);
        gdcm::StringFilter tags;
        tags.SetFile(reader.GetFile());
        const auto uid = tags.ToString(gdcm::Tag(0x0020, 0x000e));
        const auto &image = reader.GetImage();
        const auto &format = image.GetPixelFormat();
        if (format.GetSamplesPerPixel() != 1 || format.GetBitsAllocated() != 16 ||
            (image.GetNumberOfDimensions() > 2 && image.GetDimension(2) != 1))
            throw std::runtime_error("Expected single-frame, 16-bit monochrome DICOM slices");
        if (samples.empty())
        {
            series = uid;
            dimensions = {int(image.GetDimension(0)), int(image.GetDimension(1)), int(files.size()), 0};
            spacing[0] = image.GetSpacing(0);
            spacing[1] = image.GetSpacing(1);
            spacing[2] = files.size() > 1 ? sorter.GetZSpacing() : image.GetSpacing(2);
            if (spacing[2] <= 0) throw std::runtime_error("Invalid or nonuniform slice spacing");
        }
        if (uid != series || int(image.GetDimension(0)) != dimensions[0] || int(image.GetDimension(1)) != dimensions[1])
            throw std::runtime_error("DICOM directory contains incompatible slices or multiple series");
        std::vector<char> bytes(image.GetBufferLength());
        if (!image.GetBuffer(bytes.data()) || bytes.size() != size_t(dimensions[0]) * dimensions[1] * 2)
            throw std::runtime_error("Unexpected DICOM pixel buffer");
        for (size_t i = 0; i < bytes.size(); i += 2)
        {
            uint16_t raw;
            std::memcpy(&raw, bytes.data() + i, 2);
            const unsigned bits = format.GetBitsStored();
            if (bits == 0 || bits > 16) throw std::runtime_error("Invalid DICOM stored bits");
            int value = raw & ((1u << bits) - 1);
            if (format.GetPixelRepresentation() && (value & (1u << (bits - 1)))) value -= 1u << bits;
            value = int(std::lround(value * image.GetSlope() + image.GetIntercept()));
            samples.push_back(uint16_t(int16_t(std::clamp(value, -32768, 32767))));
        }
    }
    voxels.assign((samples.size() + 1) / 2, 0);
    for (size_t i = 0; i < samples.size(); ++i) voxels[i / 2] |= uint32_t(samples[i]) << ((i % 2) * 16);
    for (int i = 0; i < 3; ++i) halfSize[i] = float(dimensions[i] * spacing[i] * 0.5);
    std::cout << "Loaded DICOM volume: " << dimensions[0] << " x " << dimensions[1] << " x " << dimensions[2]
              << "; spacing " << spacing[0] << ", " << spacing[1] << ", " << spacing[2] << " mm" << std::endl;
}
void VolumeRendering::initializeScene(VkPhysicalDevice physicalDevice, VkDevice logicalDevice)
{
    physical = physicalDevice;
    device = logicalDevice;
    if (voxels.empty()) throw std::runtime_error("No DICOM volume loaded");
    const VkDeviceSize size = voxels.size() * sizeof(uint32_t);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    if (size > properties.limits.maxStorageBufferRange) throw std::runtime_error("Volume exceeds GPU storage buffer limit");
    createStorageBuffer(physical, device, size, inputBuffer, inputMemory,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void *mapped;
    check(vkMapMemory(device, inputMemory, 0, size, 0, &mapped), "Map volume");
    std::memcpy(mapped, voxels.data(), size);
    vkUnmapMemory(device, inputMemory);
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1; layout.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &descriptorLayout), "Volume descriptor layout");
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1; pool.poolSizeCount = 1; pool.pPoolSizes = &poolSize;
    check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptorPool), "Volume descriptor pool");
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptorPool; alloc.descriptorSetCount = 1; alloc.pSetLayouts = &descriptorLayout;
    check(vkAllocateDescriptorSets(device, &alloc, &descriptorSet), "Volume descriptor");
    VkDescriptorBufferInfo buffer{inputBuffer, 0, size};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &buffer;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ViewConstants)};
    VkPipelineLayoutCreateInfo pipeline{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &descriptorLayout; pipeline.pushConstantRangeCount = 1; pipeline.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device, &pipeline, nullptr, &pipelineLayout), "Volume pipeline layout");
}
void VolumeRendering::createScenePipeline(VkRenderPass renderPass)
{
    releaseScenePipeline();
    graphicsPipeline = volumePipeline(device, renderPass, pipelineLayout, "volume.vert.spv", true);
}
void VolumeRendering::releaseScenePipeline()
{
    if (device) vkDestroyPipeline(device, graphicsPipeline, nullptr);
    graphicsPipeline = VK_NULL_HANDLE;
}
void VolumeRendering::releaseScene()
{
    if (!device) return;
    releaseScenePipeline();
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    vkDestroyBuffer(device, inputBuffer, nullptr);
    vkFreeMemory(device, inputMemory, nullptr);
    device = VK_NULL_HANDLE;
}
void VolumeRendering::recordScene(VkCommandBuffer command, const Matrix4 &matrix, VkExtent2D)
{
    ViewConstants push{};
    const auto inverse = glm::inverse(glm::make_mat4(matrix.data()));
    std::memcpy(push.inverse.data(), glm::value_ptr(inverse), 64);
    push.halfSize = halfSize;
    push.dimensions = dimensions;
    push.transfer = {windowCenter, windowWidth, opacity, vesselMode ? 1.0f : 0.0f};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command, 3, 1, 0, 0);
}

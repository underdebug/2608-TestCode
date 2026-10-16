#include "SurfaceGeometry.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>


using Matrix4 = std::array<float, 16>;
struct ViewConstants
{
    Matrix4 matrix{};
    int mode = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
    int padding = 0;
};
static_assert(sizeof(ViewConstants) == 80);
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

static VkPipeline linePipeline(VkDevice device, VkRenderPass renderPass,
                               VkPipelineLayout pipelineLayout, const char *vertexShader, bool depthWrite)
{
    // Vertex and fragment colors connect through location 0.
    VkShaderModule vert = shader(device, vertexShader), frag = shader(device, "nurbs.frag.spv");
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

    // 输入阶段：无顶点属性，使用独立线段拓扑。
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
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

    // 深度阶段：较近的线段覆盖较远的线段，同时写入深度值。
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
    check(result, "Create line pipeline");
    return pipeline;
}

SurfaceGeometry::SurfaceGeometry(QWindow *parent) : VulkanWindow(parent)
{
}
SurfaceGeometry::~SurfaceGeometry()
{
    // Release while derived hooks and shader resources are still alive.
    VulkanWindow::release();
    releaseScene();
}

void SurfaceGeometry::setInputPoints(const std::vector<InputPoint> &points, int rows, int cols)
{
    if (rows < 4 || rows > 16 || cols < 4 || cols > 16 ||
        points.size() != static_cast<size_t>(rows * cols))
        throw std::invalid_argument("Input grid needs 4..16 rows/cols and rows * cols points");
    for (const auto &point : points)
        if (!std::all_of(point.begin(), point.end(), [](float v) { return std::isfinite(v); }) || point[3] != 1.0f)
            throw std::invalid_argument("Input coordinates must be finite and weights must be 1");
    m_inputPoints = points;
    m_rows = rows;
    m_cols = cols;
    m_inputDirty = true;
    requestUpdate();
}
void SurfaceGeometry::initializeScene(VkPhysicalDevice physicalDevice, VkDevice logicalDevice)
{
    if (m_inputPoints.empty())
        throw std::logic_error("Call setInputPoints before initialize");
    if (device)
        return;
    physical = physicalDevice;
    device = logicalDevice;
    try
    {
        initResources();
        m_inputDirty = true;
    }
    catch (...)
    {
        releaseScene();
        throw;
    }
}

void SurfaceGeometry::releaseScene()
{
    releaseResources();
    physical = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    m_inputDirty = true;
}

void SurfaceGeometry::releaseScenePipeline()
{
    if (!device)
        return;
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    graphicsPipeline = VK_NULL_HANDLE;
}

void SurfaceGeometry::createScenePipeline(VkRenderPass renderPass)
{
    releaseScenePipeline();
    graphicsPipeline = linePipeline(device, renderPass, pipelineLayout, "nurbs.vert.spv", true);
}

void SurfaceGeometry::initResources()
{
    createStorageBuffer(physical, device, inputBufferSize, inputBuffer, inputMemory,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    createStorageBuffer(physical, device, displayBufferSize, displayBuffer, displayMemory);
    initDescriptors();
    initPipelineLayout();
    initComputePipeline();
}

void SurfaceGeometry::initDescriptors()
{
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dc.bindingCount = 2;
    dc.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(device, &dc, nullptr, &descriptorLayout), "Create descriptors");
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
    VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pc.maxSets = 1;
    pc.poolSizeCount = 1;
    pc.pPoolSizes = sizes;
    check(vkCreateDescriptorPool(device, &pc, nullptr, &descriptorPool), "Create pool");
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = descriptorPool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &descriptorLayout;
    check(vkAllocateDescriptorSets(device, &da, &descriptorSet), "Allocate descriptors");
    VkDescriptorBufferInfo infos[] = {{inputBuffer, 0, inputBufferSize},
                                     {displayBuffer, 0, displayBufferSize}};
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t i = 0; i < 2; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
}

void SurfaceGeometry::initPipelineLayout()
{
    // 每帧直接传递相机矩阵和显示模式：64 字节矩阵 + 模式及填充，共 80 字节。
    VkPushConstantRange push[] = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ViewConstants)},
                                  {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CalculationConstants)}};
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.setLayoutCount = 1;
    lc.pSetLayouts = &descriptorLayout;
    lc.pushConstantRangeCount = 2;
    lc.pPushConstantRanges = push;
    check(vkCreatePipelineLayout(device, &lc, nullptr, &pipelineLayout), "Create pipeline layout");
}

void SurfaceGeometry::initComputePipeline()
{
    VkShaderModule module = shader(device, "nurbs.comp.spv");
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = module;
    ci.stage.pName = "main";
    ci.layout = pipelineLayout;
    const auto result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci,
                                                 nullptr, &computePipeline);
    vkDestroyShaderModule(device, module, nullptr);
    check(result, "Create compute pipeline");
}

void SurfaceGeometry::calculateScene(VkCommandBuffer command, VkQueue queue, VkFence submitted)
{
    if (!device || !m_inputDirty)
        return;
    // The caller has waited for the previous frame before updating shared buffers.
    void *mapped = nullptr;
    check(vkMapMemory(device, inputMemory, 0, inputBufferSize, 0, &mapped), "Map input points");
    std::memcpy(mapped, m_inputPoints.data(), m_inputPoints.size() * pointSize);
    vkUnmapMemory(device, inputMemory);
    check(vkResetCommandBuffer(command, 0), "Reset calculation command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "Begin point calculation");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                            0, 1, &descriptorSet, 0, nullptr);

    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = displayBuffer;
    barrier.size = VK_WHOLE_SIZE;
    // Two interpolation passes, then display generation.
    for (int phase = 1; phase < 4; ++phase)
    {
        CalculationConstants constants{phase, m_rows, m_cols};
        vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(constants), &constants);
        const uint32_t invocationCount = phase == 1 ? m_cols :
                                          (phase == 2 ? m_rows : displayPointCount);
        const uint32_t groupCount =
            (invocationCount + computeGroupSize - 1) / computeGroupSize;
        vkCmdDispatch(command, groupCount, 1, 1);
        if (phase < 3)
        {
            VkBufferMemoryBarrier dependency = barrier;
            dependency.buffer = displayBuffer;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 0, nullptr, 1, &dependency, 0, nullptr);
        }
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0,
                         0, nullptr, 1, &barrier, 0, nullptr);
    check(vkEndCommandBuffer(command), "End point calculation");
    check(vkResetFences(device, 1, &submitted), "Reset calculation fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(queue, 1, &submit, submitted), "Calculate display points");
    check(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX),
          "Wait for display points");
    m_inputDirty = false;
}

void SurfaceGeometry::releaseResources()
{
    if (!device)
        return;
    releaseScenePipeline();
    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyBuffer(device, displayBuffer, nullptr);
    vkFreeMemory(device, displayMemory, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    vkDestroyBuffer(device, inputBuffer, nullptr);
    vkFreeMemory(device, inputMemory, nullptr);
    computePipeline = VK_NULL_HANDLE;
    displayBuffer = VK_NULL_HANDLE;
    displayMemory = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorLayout = VK_NULL_HANDLE;
    descriptorSet = VK_NULL_HANDLE;
    inputBuffer = VK_NULL_HANDLE;
    inputMemory = VK_NULL_HANDLE;
}

void SurfaceGeometry::recordScene(VkCommandBuffer command, const Matrix4 &matrix, VkExtent2D extent)
{
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &descriptorSet, 0, nullptr);
    ViewConstants push{};
    push.matrix = matrix;
    push.viewportWidth = int(extent.width);
    push.viewportHeight = int(extent.height);
    const bool visible[] = {gridVisible, pointsVisible, controlPointsVisible};
    for (int mode = 0; mode < 3; ++mode)
        if (visible[mode])
        {
            push.mode = mode;
            vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDraw(command, mode == 0 ? gridVertexCount : uint32_t(m_rows * m_cols * 6), 1, 0, 0);
        }
}

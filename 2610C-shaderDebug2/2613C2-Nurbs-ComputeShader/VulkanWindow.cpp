#include "VulkanWindow.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <QCoreApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QWheelEvent>
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
#include <limits>


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

// Prefer the clicked pixel, otherwise the valid sample closest in screen space.
// Return the sample location so callers can choose how to use its depth.
static bool nearestDepthPixel(const void *data, bool floatingPoint, int width, int height,
                       int &x, int &y, float &depth)
{
    const auto readDepth = [data, floatingPoint, width](int px, int py)
    {
        const size_t index = size_t(py) * width + px;
        if (floatingPoint)
            return static_cast<const float *>(data)[index];
        return static_cast<const uint16_t *>(data)[index] / 65535.0f;
    };
    const auto valid = [](float value)
    {
        return std::isfinite(value) && value >= 0.0f && value < 1.0f;
    };
    depth = readDepth(x, y);
    if (valid(depth))
        return true;

    int bestX = x, bestY = y;
    int64_t bestDistance = std::numeric_limits<int64_t>::max();
    for (int py = 0; py < height; ++py)
        for (int px = 0; px < width; ++px)
        {
            const int64_t dx = int64_t(px) - x, dy = int64_t(py) - y;
            const int64_t distance = dx * dx + dy * dy;
            if (distance >= bestDistance)
                continue;
            const float candidate = readDepth(px, py);
            if (valid(candidate))
            {
                bestDistance = distance;
                bestX = px;
                bestY = py;
                depth = candidate;
            }
        }
    if (bestDistance == std::numeric_limits<int64_t>::max())
        return false;
    x = bestX;
    y = bestY;
    return true;
}

// 单位矩阵：旋转、平移矩阵都从它开始设置。
static Matrix4 identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// 矩阵乘法 C=A*B，元素下标为 column*4+row；列向量先乘 B，再乘 A。
static Matrix4 multiply(const Matrix4 &a, const Matrix4 &b)
{
    Matrix4 result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}

static std::array<qreal, 4> projectionBounds(qreal distance, qreal aspect)
{
    const qreal halfHeight = distance * std::tan(3.14159265358979323846 / 8.0);
    return {-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight};
}

// Translate the coordinate origin to zero before rotating around it.
static Matrix4 viewProjection(float spin, float tilt, float distance, float aspect, float panX, float panY,
                       const std::array<float, 3> &coordinate, float panZ)
{
    constexpr float radians = 3.14159265358979323846f / 180.0f;
    const float sx = std::sin(tilt * radians), cx = std::cos(tilt * radians);
    const float sy = std::sin(spin * radians), cy = std::cos(spin * radians);
    Matrix4 rx = identity(), ry = identity(), translation = identity();
    rx[5] = cx;
    rx[6] = sx;
    rx[9] = -sx;
    rx[10] = cx;
    ry[0] = cy;
    ry[2] = -sy;
    ry[8] = sy;
    ry[10] = cy;
    translation[12] = panX;
    translation[13] = panY;
    // Orthographic zoom changes the visible bounds, independently of depth clipping.
    translation[14] = -10000.0f + panZ;
    // Match the reference's orthographic view: pan has one scale at every depth.
    // Keep distance as the zoom control, preserving the previous origin-plane size.
    const auto bounds = projectionBounds(distance, aspect);
    const float nearPlane = 1, farPlane = 20000;
    // Explicit right-handed, Vulkan [0,1] depth; reversed Y bounds flip screen Y.
    const glm::mat4 ortho = glm::orthoRH_ZO(float(bounds[0]), float(bounds[1]),
                                           float(bounds[3]), float(bounds[2]), nearPlane, farPlane);
    Matrix4 projection{};
    std::memcpy(projection.data(), glm::value_ptr(ortho), sizeof(projection));
    Matrix4 pivot = identity();
    pivot[12] = -coordinate[0];
    pivot[13] = -coordinate[1];
    pivot[14] = -coordinate[2];
    return multiply(projection, multiply(translation, multiply(multiply(rx, ry), pivot)));
}



class VulkanWindow::Renderer
{
    // 窗口来源：Qt 拥有原生窗口；渲染器单独打开 X11 连接。
    VulkanWindow *owner;         // 相机参数、显示开关和像素尺寸。
    unsigned long nativeWindow; // Qt 传入的 X11 窗口 ID，不负责销毁窗口。
    Display *display = nullptr; // 渲染器拥有的 X11 连接。

    // Vulkan 上下文：实例发现 GPU，逻辑设备分配资源并提交命令。
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0; // Graphics, compute and presentation share one queue.

    // 交换链：窗口尺寸变化时重建，views 和 framebuffers 按图像索引对应。
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    bool dirty = true; // 下一次绘制前需要重建交换链。

    // 深度缓冲：当前只有一帧在 GPU 上执行，各交换链图像共享它。
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkBuffer pickBuffer = VK_NULL_HANDLE;
    VkDeviceMemory pickMemory = VK_NULL_HANDLE;
    VkDeviceSize pickBufferSize = 0;
    Matrix4 renderedMatrix{};
    bool hasFrame = false;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline overlayPipeline = VK_NULL_HANDLE;

    // 命令记录：命令池分配 command，每次绘制重新记录。
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;

    // 同步：acquired 等图像可用，submitted 等 GPU 完成这一帧。
    // rendered 每个交换链图像一个；重新获取同一图像后才能安全复用。
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    std::vector<VkSemaphore> rendered;

    // 图像视图描述如何使用 VkImage；这里固定为单层、单 mip 的二维附件。
    VkImageView imageView(VkImage image, VkFormat format, VkImageAspectFlags aspect)
    {
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ci.image = image;
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = format;
        ci.subresourceRange = {aspect, 0, 1, 0, 1};
        VkImageView view;
        check(vkCreateImageView(device, &ci, nullptr, &view), "Create image view");
        return view;
    }

    // 创建 Vulkan 实例、X11 表面和逻辑设备，选择绘制/显示队列。
    void initDevice()
    {
        // 建立实例和原生表面；窗口本身仍由 Qt 管理。
        display = XOpenDisplay(nullptr);
        if (!display)
            fail("Cannot open the X11 display");
        const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                    VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "NURBS Vulkan";
        application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ic.pApplicationInfo = &application;
        ic.enabledExtensionCount = 2;
        ic.ppEnabledExtensionNames = extensions;
        check(vkCreateInstance(&ic, nullptr, &instance), "Create Vulkan instance");
        VkXlibSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
        surfaceInfo.dpy = display;
        surfaceInfo.window = nativeWindow;
        check(vkCreateXlibSurfaceKHR(instance, &surfaceInfo, nullptr, &surface),
              "Create X11 Vulkan surface");

        // 枚举 GPU，要求支持交换链和同一个绘制/显示队列族。
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Enumerate GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Enumerate GPUs");
        for (auto candidate : devices)
        {
            uint32_t extensionCount = 0;
            check(
                vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr),
                "Enumerate extensions");
            std::vector<VkExtensionProperties> extensions(extensionCount);
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount,
                                                       extensions.data()),
                  "Enumerate extensions");
            if (std::none_of(
                    extensions.begin(), extensions.end(), [](const auto &e)
                    { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; }))
                continue;
            uint32_t queueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
            for (uint32_t i = 0; i < queueCount; ++i)
            {
                VkBool32 present = VK_FALSE;
                check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present),
                      "Query presentation support");
                if ((queues[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) && present)
                {
                    physical = candidate;
                    family = i;
                    break;
                }
            }
            if (physical)
                break;
        }
        if (!physical)
            fail("No Vulkan GPU with a combined graphics/compute/presentation queue");

        // 创建逻辑设备并取出队列；这里只使用一个队列。
        float priority = 1;
        VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qc.queueFamilyIndex = family;
        qc.queueCount = 1;
        qc.pQueuePriorities = &priority;
        const char *extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dc.queueCreateInfoCount = 1;
        dc.pQueueCreateInfos = &qc;
        dc.enabledExtensionCount = 1;
        dc.ppEnabledExtensionNames = &extension;
        check(vkCreateDevice(physical, &dc, nullptr, &device), "Create device");
        vkGetDeviceQueue(device, family, 0, &queue);

        // 分配命令缓冲和同步对象；fence 初始为已完成，允许第一次绘制。
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pc.queueFamilyIndex = family;
        pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &pc, nullptr, &commandPool), "Create command pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = commandPool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &ca, &command), "Allocate command buffer");
        VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &sc, nullptr, &acquired), "Create acquire semaphore");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fc, nullptr, &submitted), "Create submission fence");

        // 选择设备支持的深度格式，供遮挡测试使用。
        for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM})
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
            if ((properties.optimalTilingFeatures & (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) ==
                (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
            {
                depthFormat = format;
                break;
            }
        }
        if (depthFormat == VK_FORMAT_UNDEFINED)
            fail("No supported depth format");
    }

    // 根据窗口像素尺寸建立显示图像、共享深度缓冲和图形管线。
    bool createSwapchain()
    {
        // 查询窗口限制；最小化或零尺寸时暂不创建交换链。
        VkSurfaceCapabilitiesKHR caps;
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
              "Query surface capabilities");

        const int width = owner->m_width, height = owner->m_height;
        if (width <= 0 || height <= 0)
            return false;
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
            extent = caps.currentExtent;
        else
        {
            extent.width =
                std::clamp(uint32_t(width), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(uint32_t(height), caps.minImageExtent.height,
                                       caps.maxImageExtent.height);
        }
        if (!extent.width || !extent.height)
            return false;
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            fail("Surface cannot be a color attachment");
        uint32_t count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr),
              "Query surface formats");

        // 选择颜色格式，优先使用 BGRA8 UNORM；使用 FIFO 显示模式。
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()),
              "Query surface formats");
        if (formats.empty())
            fail("No surface formats");
        auto chosen = formats.front();
        if (chosen.format == VK_FORMAT_UNDEFINED)
            chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
        for (auto format : formats)
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                chosen = format;
        colorFormat = chosen.format;
        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount)
            imageCount = std::min(imageCount, caps.maxImageCount);
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto option :
             {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & option)
            {
                alpha = option;
                break;
            }

        // 创建交换链并为每张显示图像建立颜色视图。
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface;
        ci.minImageCount = imageCount;
        ci.imageFormat = colorFormat;
        ci.imageColorSpace = chosen.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = alpha;
        ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        ci.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain), "Create swap chain");
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "Get swap chain images");
        std::vector<VkImage> images(count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()),
              "Get swap chain images");
        for (auto image : images)
            views.push_back(imageView(image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT));

        // 创建深度图像、分配设备内存并绑定，再建立深度视图。
        VkImageCreateInfo depth{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        depth.imageType = VK_IMAGE_TYPE_2D;
        depth.format = depthFormat;
        depth.extent = {extent.width, extent.height, 1};
        depth.mipLevels = 1;
        depth.arrayLayers = 1;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.tiling = VK_IMAGE_TILING_OPTIMAL;
        depth.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        check(vkCreateImage(device, &depth, nullptr, &depthImage), "Create depth image");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, depthImage, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex =
            memoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &depthMemory),
              "Allocate depth memory");
        check(vkBindImageMemory(device, depthImage, depthMemory, 0), "Bind depth memory");
        depthView = imageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        // 定义渲染通道：清空颜色/深度，结束后颜色图像进入显示布局。
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        subpass.pDepthStencilAttachment = &depthRef;

        // 同步颜色和深度附件访问；布局转换由 render pass 完成。
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = dependency.srcStageMask;
        dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 2;
        rp.pAttachments = attachments;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = 1;
        rp.pDependencies = &dependency;
        check(vkCreateRenderPass(device, &rp, nullptr, &renderPass), "Create render pass");

        // 每张颜色图像配一个 framebuffer 和一个显示完成信号量。
        for (auto view : views)
        {
            VkImageView attachments[] = {view, depthView};
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = renderPass;
            fb.attachmentCount = 2;
            fb.pAttachments = attachments;
            fb.width = extent.width;
            fb.height = extent.height;
            fb.layers = 1;
            VkFramebuffer framebuffer;
            check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffer), "Create framebuffer");
            framebuffers.push_back(framebuffer);
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore finished;
            check(vkCreateSemaphore(device, &semaphore, nullptr, &finished),
                  "Create presentation semaphore");
            rendered.push_back(finished);
        }
        owner->createScenePipeline(renderPass);
        overlayPipeline = linePipeline(device, renderPass, pipelineLayout, "overlay.vert.spv", false);
        dirty = false;
        return true;
    }

    // 等待 GPU 停止使用资源，再释放与窗口尺寸相关的对象。
    void destroySwapchain()
    {
        if (!swapchain)
            return;
        vkDeviceWaitIdle(device);
        hasFrame = false;
        vkDestroyPipeline(device, overlayPipeline, nullptr);
        overlayPipeline = VK_NULL_HANDLE;
        owner->releaseScenePipeline();
        for (auto framebuffer : framebuffers)
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffers.clear();
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
        vkDestroyImageView(device, depthView, nullptr);
        depthView = VK_NULL_HANDLE;
        vkDestroyImage(device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
        vkFreeMemory(device, depthMemory, nullptr);
        depthMemory = VK_NULL_HANDLE;
        for (auto view : views)
            vkDestroyImageView(device, view, nullptr);
        views.clear();
        for (auto semaphore : rendered)
            vkDestroySemaphore(device, semaphore, nullptr);
        rendered.clear();
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    // 按依赖的逆序释放资源：设备对象 -> 表面 -> 实例 -> X11 连接。
    void cleanup() noexcept
    {
        if (device)
        {
            vkDeviceWaitIdle(device);
            destroySwapchain();
            owner->releaseScene();
            vkDestroyBuffer(device, pickBuffer, nullptr);
            vkFreeMemory(device, pickMemory, nullptr);
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vkDestroyFence(device, submitted, nullptr);
            vkDestroySemaphore(device, acquired, nullptr);
            vkDestroyCommandPool(device, commandPool, nullptr);
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance)
            vkDestroyInstance(instance, nullptr);
        surface = VK_NULL_HANDLE;
        instance = VK_NULL_HANDLE;
        if (display)
            XCloseDisplay(display);
        display = nullptr;
    }

  public:
    // 初始化失败也清理已经创建的资源，然后把异常交给 UI。
    Renderer(VulkanWindow *surfaceOwner, unsigned long windowId)
        : owner(surfaceOwner), nativeWindow(windowId)
    {
        try
        {
            initDevice();
            owner->initializeScene(physical, device);
            const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ViewConstants)};
            VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layout.pushConstantRangeCount = 1;
            layout.pPushConstantRanges = &push;
            check(vkCreatePipelineLayout(device, &layout, nullptr, &pipelineLayout), "Create overlay layout");
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }
    ~Renderer()
    {
        cleanup();
    }

    // 只标记交换链过期，实际重建留到下一次 draw()。
    void resize()
    {
        dirty = true;
    }

  private:
    void recordFrame(uint32_t image)
    {
        // 开始记录命令，设置清屏、管线、描述符和窗口像素范围。
        check(vkResetCommandBuffer(command, 0), "Reset command buffer");
        VkCommandBufferBeginInfo commandBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        commandBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &commandBegin), "Begin command buffer");
        auto cb = command;
        VkClearValue clear[2]{};
        clear[0].color = {{0.063f, 0.094f, 0.153f, 1}};
        clear[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = renderPass;
        begin.framebuffer = framebuffers[image];
        begin.renderArea.extent = extent;
        begin.clearValueCount = 2;
        begin.pClearValues = clear;
        vkCmdBeginRenderPass(cb, &begin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, begin.renderArea.extent};
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);

        // 计算相机变换；push constants 为每次绘制指定显示模式。
        ViewConstants push{};
        push.viewportWidth = int(extent.width);
        push.viewportHeight = int(extent.height);
        push.matrix = viewProjection(owner->spin, owner->tilt, owner->distance,
                                     float(extent.width) / float(extent.height), owner->panX, owner->panY,
                                     owner->coordinate, owner->panZ);

        owner->recordScene(cb, push.matrix, extent);
        renderedMatrix = push.matrix;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipeline);
        // World coordinate axes share the surface camera transform.
        const Matrix4 sceneMatrix = push.matrix;
        Matrix4 axisTranslation = identity();
        axisTranslation[12] = owner->coordinate[0];
        axisTranslation[13] = owner->coordinate[1];
        axisTranslation[14] = owner->coordinate[2];
        push.matrix = multiply(sceneMatrix, axisTranslation);
        // Use one common world-space length, normalized by the longest projected axis.
        // Scale only the basis columns so the movable origin stays in place.
        float longestPixels = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float dx = push.matrix[axis * 4] * float(extent.width) * 0.5f;
            const float dy = push.matrix[axis * 4 + 1] * float(extent.height) * 0.5f;
            longestPixels = std::max(longestPixels, std::hypot(dx, dy));
        }
        const float axisLength = (float(extent.width) / 5.0f) / std::max(longestPixels, 1e-6f);
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 4; ++row)
                push.matrix[column * 4 + row] *= axisLength;
        push.mode = 4;
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cb, 30, 1, 0, 0);

        // Orthographic ruler scale matches pan translation at every scene depth.
        const auto bounds = owner->orthographicBounds();
        const float scale = float((bounds[3] - bounds[2]) / extent.height);
        const float rawStep = scale * std::max(extent.width, extent.height) / 8.0f;
        const float magnitude = std::pow(10.0f, std::floor(std::log10(rawStep)));
        const float normalized = rawStep / magnitude;
        const float step = magnitude * (normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10);
        push.mode = 3;
        push.matrix = {};
        push.matrix[0] = float(extent.width);
        push.matrix[1] = float(extent.height);
        push.matrix[2] = scale;
        push.matrix[3] = step;
        constexpr float radians = 3.14159265358979323846f / 180.0f;
        const float sx = std::sin(owner->tilt * radians), cx = std::cos(owner->tilt * radians);
        const float sy = std::sin(owner->spin * radians), cy = std::cos(owner->spin * radians);
        const auto &c = owner->coordinate;
        const float axisX = cy * c[0] + sy * c[2];
        const float axisY = sx * sy * c[0] + cx * c[1] - sx * cy * c[2];
        push.matrix[4] = owner->panX - axisX;
        push.matrix[5] = owner->panY - axisY;
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cb, 2 * (2 + 2 * 16 * 57), 1, 0, 0);
        vkCmdEndRenderPass(cb);
        check(vkEndCommandBuffer(cb), "End command buffer");
    }

  public:
    bool pick(int x, int y, std::array<float, 3> &position)
    {
        if (!hasFrame || dirty || x < 0 || y < 0 ||
            x >= int(extent.width) || y >= int(extent.height))
            return false;
        check(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX), "Wait for pick frame");
        const VkDeviceSize readbackSize = VkDeviceSize(extent.width) * extent.height *
                                         (depthFormat == VK_FORMAT_D32_SFLOAT ? 4 : 2);
        if (readbackSize > pickBufferSize)
        {
            vkDestroyBuffer(device, pickBuffer, nullptr);
            vkFreeMemory(device, pickMemory, nullptr);
            pickBuffer = VK_NULL_HANDLE;
            pickMemory = VK_NULL_HANDLE;
            createStorageBuffer(physical, device, readbackSize, pickBuffer, pickMemory,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            pickBufferSize = readbackSize;
        }
        check(vkResetCommandBuffer(command, 0), "Reset pick command");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin), "Begin depth pick");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = depthImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(command, depthImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               pickBuffer, 1, &copy);
        std::swap(barrier.oldLayout, barrier.newLayout);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        check(vkEndCommandBuffer(command), "End depth pick");
        check(vkResetFences(device, 1, &submitted), "Reset pick fence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, submitted), "Submit depth pick");
        check(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX), "Wait for depth pick");
        void *mapped = nullptr;
        check(vkMapMemory(device, pickMemory, 0, readbackSize, 0, &mapped), "Map depth pick");
        float depth;
        int sampleX = x, sampleY = y;
        const bool found = nearestDepthPixel(mapped, depthFormat == VK_FORMAT_D32_SFLOAT,
                                             int(extent.width), int(extent.height), sampleX, sampleY, depth);
        vkUnmapMemory(device, pickMemory);
        if (!found)
            return false;
        // Keep the clicked screen position; borrow only the nearest valid depth.
        const glm::vec4 clip(2.0f * (x + 0.5f) / extent.width - 1.0f,
                             2.0f * (y + 0.5f) / extent.height - 1.0f, depth, 1.0f);
        const glm::vec4 world = glm::inverse(glm::make_mat4(renderedMatrix.data())) * clip;
        for (int i = 0; i < 3; ++i)
            position[i] = world[i] / world.w;
        return std::all_of(position.begin(), position.end(), [](float v) { return std::isfinite(v); });
    }

    // 一帧：等待上一帧 -> 获取图像 -> 记录命令 -> 提交 -> 显示。
    void draw()
    {
        // 检查尺寸并等待上一帧完成，然后按需重建交换链。
        const int width = owner->m_width, height = owner->m_height;
        if (width <= 0 || height <= 0)
        {
            owner->requestUpdate();
            return;
        }
        check(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX),
              "Wait for submitted frame");
        owner->calculateScene(command, queue, submitted);
        if (dirty)
        {
            destroySwapchain();
            if (!createSwapchain())
                return;
        }

        // 获取显示图像；尺寸变化导致 OUT_OF_DATE 时请求下一次重建。
        uint32_t image = 0;
        VkResult result =
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &image);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            dirty = true;
            owner->requestUpdate();
            return;
        }
        if (result != VK_SUBOPTIMAL_KHR)
            check(result, "Acquire swap chain image");
        bool recreate = result == VK_SUBOPTIMAL_KHR;

        recordFrame(image);

        // 提交命令：等待图像可用，完成后通知显示队列并标记 fence。
        check(vkResetFences(device, 1, &submitted), "Reset submission fence");
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &rendered[image];
        check(vkQueueSubmit(queue, 1, &submit, submitted), "Submit frame");
        hasFrame = true;

        // 显示当前图像；显示阶段也可能发现交换链过期。
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &rendered[image];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image;
        result = vkQueuePresentKHR(queue, &present);
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
            check(result, "Present frame");
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || recreate)
        {
            dirty = true;
            owner->requestUpdate();
        }
    }
};

VulkanWindow::VulkanWindow(QWindow *parent)
    : QWindow(parent), pan(*this), rotate(*this), zoom(*this), axis(*this)
{
}
VulkanWindow::~VulkanWindow()
{
    release();
}

std::array<qreal, 4> VulkanWindow::orthographicBounds() const
{
    const qreal aspect = m_width > 0 && m_height > 0
        ? qreal(m_width) / m_height : qreal(width()) / std::max(height(), 1);
    return projectionBounds(distance, aspect);
}

void VulkanWindow::release()
{
    m_renderer.reset();
}
void VulkanWindow::render()
{
    const int framebufferWidth = int(width() * devicePixelRatio());
    const int framebufferHeight = int(height() * devicePixelRatio());
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
        return;
    if (framebufferWidth != m_width || framebufferHeight != m_height)
    {
        m_width = framebufferWidth;
        m_height = framebufferHeight;
        if (m_renderer)
            m_renderer->resize();
    }
    if (!m_renderer)
        m_renderer = std::make_unique<Renderer>(this, static_cast<unsigned long>(winId()));
    m_renderer->draw();
}
bool VulkanWindow::pickPosition(int x, int y, std::array<float, 3> &position)
{
    return m_renderer && m_renderer->pick(x, y, position);
}

void VulkanWindow::keyPressEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat() &&
        (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Control ||
         event->key() == Qt::Key_Alt || event->key() == Qt::Key_Meta))
    {
        // Start modifier navigation at the current cursor, just as a
        // mouse press starts dragging at the press position.
        lastMousePosition = mapFromGlobal(QCursor::pos());
        hasMousePosition = true;
    }
    QWindow::keyPressEvent(event);
}

void VulkanWindow::wheelEvent(QWheelEvent *event)
{
    if (!axis.wheel(*event) && !zoom.wheel(*event))
        QWindow::wheelEvent(event);
}

void VulkanWindow::mousePressEvent(QMouseEvent *event)
{
    lastMousePosition = event->position();
    hasMousePosition = true;
    if (axis.press(*event))
        return;
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
    {
        event->accept();
        return;
    }
    QWindow::mousePressEvent(event);
}

void VulkanWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    // A double click's second press is delivered as MouseButtonDblClick.
    mousePressEvent(event);
}

void VulkanWindow::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF delta = hasMousePosition
        ? event->position() - lastMousePosition : QPointF();
    lastMousePosition = event->position();
    hasMousePosition = true;
    if (axis.move(*event, delta) || pan.move(*event, delta) || rotate.move(*event, delta))
        return;
    QWindow::mouseMoveEvent(event);
}

void VulkanWindow::exposeEvent(QExposeEvent *)
{
    if (isExposed())
        requestUpdate();
}

void VulkanWindow::resizeEvent(QResizeEvent *)
{
    if (isExposed())
        requestUpdate();
}

bool VulkanWindow::event(QEvent *event)
{
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        hasMousePosition = false;
    if (event->type() == QEvent::PlatformSurface)
    {
        auto *platformEvent = static_cast<QPlatformSurfaceEvent *>(event);
        if (platformEvent->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
        {
            // Release Vulkan resources before Qt destroys the native window.
            release();
        }
    }
    else if (event->type() == QEvent::UpdateRequest)
    {
        if (isExposed())
        {
            try
            {
                render();
            }
            catch (const std::exception &error)
            {
                QMessageBox::critical(nullptr, "Vulkan renderer",
                                      QString::fromUtf8(error.what()));
                QCoreApplication::exit(1);
            }
        }
        return true;
    }
    return QWindow::event(event);
}


#include "SurfaceGeometry.h"
#include "surface.h"
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// shader comment begin Surface 
// layout(binding = 0, std140) uniform Surface
// {
//     vec4 control[100]; // xyz in world coordinates, w is the rational weight
//     vec4 inputPoint[100];
// } surface;
// shader comment end Surface 


// shader comment begin View 
// layout(push_constant) uniform View
// {
//     mat4 mvp;
//     int mode;
// } view;
// shader comment end View 


// shader comment begin color 
// layout(location = 0) out vec3 color;
// shader comment end color 

namespace
{

// 初始化条件不满足时抛异常，由 Qt UI 显示错误信息。
[[noreturn]] void fail(const char *message)
{
    throw std::runtime_error(message);
}

// 4×4 列主序矩阵，与 GLSL mat4 的内存顺序一致。
// 右手相机沿 -Z 看向曲面；投影翻转 Y，并把深度映射到 Vulkan 的 [0,1]。
using Matrix4 = std::array<float, 16>;

// 单位矩阵：旋转、平移矩阵都从它开始设置。
Matrix4 identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// 矩阵乘法 C=A*B，元素下标为 column*4+row；列向量先乘 B，再乘 A。
Matrix4 multiply(const Matrix4 &a, const Matrix4 &b)
{
    Matrix4 result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}

// 构造最终变换：绕 Y 旋转 -> 绕 X 倾斜 -> 相机平移 -> 透视投影。
Matrix4 viewProjection(float spin, float tilt, float distance, float aspect)
{

    /*
     *   曲面位置 p --> Ry --> Rx --> T --> P --> 裁剪坐标
     *
     *   M = P*T*Rx*Ry，着色器计算 gl_Position = M*vec4(p,1)。
     *   distance 控制沿 Z 的平移；aspect 为窗口像素宽/高。
     *   视场角 45°，近平面 1，远平面 2000。
     */
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
    translation[14] = -distance;
    const float focal = 1.0f / std::tan(22.5f * radians), nearPlane = 1, farPlane = 2000;
    Matrix4 projection{};
    projection[0] = focal / aspect;
    projection[5] = -focal;
    projection[10] = farPlane / (nearPlane - farPlane);
    projection[11] = -1;
    projection[14] = nearPlane * farPlane / (nearPlane - farPlane);
    return multiply(projection, multiply(translation, multiply(rx, ry)));
}

// 检查 Vulkan 返回值；图像过期等可恢复状态由调用处单独处理。
void check(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: VkResult " +
                                 std::to_string(result));
}
} // namespace

class SurfaceGeometry::Renderer
{
    /*
     * ====================== Vulkan 渲染流程 ======================
     *
     *   Qt 原生窗口 ID
     *        |
     *        v
     *   initDevice() --> initResources() --> createSwapchain()
     *   实例 / 设备       控制点 / 描述符      图像 / 深度 / 管线
     *                                             |
     *                                             v
     *   draw(): 获取图像 --> 记录命令 --> 提交 GPU --> 显示
     *
     *   窗口尺寸变化：destroySwapchain() --> createSwapchain()
     *   程序退出：    cleanup()，先释放设备资源，再释放实例。
     */

    // 1. 窗口来源：Qt 拥有原生窗口；渲染器单独打开 X11 连接。
    SurfaceGeometry *w;         // 相机参数、显示开关和像素尺寸。
    unsigned long nativeWindow; // Qt 传入的 X11 窗口 ID，不负责销毁窗口。
    Display *display = nullptr; // 渲染器拥有的 X11 连接。

    // 2. Vulkan 上下文：实例发现 GPU，逻辑设备分配资源并提交命令。
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0; // 同时支持绘制和显示的队列族。

    // 3. 交换链：窗口尺寸变化时重建，views 和 framebuffers 按图像索引对应。
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    bool dirty = true; // 下一次绘制前需要重建交换链。

    // 4. 深度缓冲：当前只有一帧在 GPU 上执行，各交换链图像共享它。
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;

    // 5. 曲面数据：buffer 保存控制点和输入点，描述符把它交给顶点着色器。
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;

    // 6. 绘制规则：renderPass 定义颜色/深度附件，pipeline 定义着色器和线段绘制。
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // 7. 命令记录：命令池分配 command，每次绘制重新记录。
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;

    // 8. 同步：acquired 等图像可用，submitted 等 GPU 完成这一帧。
    // rendered 每个交换链图像一个；重新获取同一图像后才能安全复用。
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    std::vector<VkSemaphore> rendered;

    // bits 给出资源允许的内存类型，flags 给出可见性/一致性等要求。
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags)
    {
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        fail("No suitable Vulkan memory type");
        return 0;
    }

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

        // 1. 建立实例和原生表面；窗口本身仍由 Qt 管理。
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

        // 2. 枚举 GPU，要求支持交换链和同一个绘制/显示队列族。
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
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
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
            fail("No Vulkan GPU with a combined graphics/presentation queue");

        // 3. 创建逻辑设备并取出队列；这里只使用一个队列。
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

        // 4. 分配命令缓冲和同步对象；fence 初始为已完成，允许第一次绘制。
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

        // 5. 选择设备支持的深度格式，供遮挡测试使用。
        for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM})
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
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

        /*
         *   swapchain 图像 i --> views[i] --+--> framebuffers[i]
         *                                 |
         *   depthImage -------> depthView -+
         *
         *   窗口尺寸决定 extent；颜色视图每张图像一个，深度附件共享。
         *   当前只允许一帧在 GPU 上执行，因此不会同时写共享深度附件。
         */

        // 1. 查询窗口限制；最小化或零尺寸时暂不创建交换链。
        VkSurfaceCapabilitiesKHR caps;
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
              "Query surface capabilities");

        const int width = w->m_width, height = w->m_height;
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

        // 2. 选择颜色格式，优先使用 BGRA8 UNORM；使用 FIFO 显示模式。
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

        // 3. 创建交换链并为每张显示图像建立颜色视图。
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

        // 4. 创建深度图像、分配设备内存并绑定，再建立深度视图。
        VkImageCreateInfo depth{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        depth.imageType = VK_IMAGE_TYPE_2D;
        depth.format = depthFormat;
        depth.extent = {extent.width, extent.height, 1};
        depth.mipLevels = 1;
        depth.arrayLayers = 1;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.tiling = VK_IMAGE_TILING_OPTIMAL;
        depth.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        check(vkCreateImage(device, &depth, nullptr, &depthImage), "Create depth image");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, depthImage, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex =
            memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &depthMemory),
              "Allocate depth memory");
        check(vkBindImageMemory(device, depthImage, depthMemory, 0), "Bind depth memory");
        depthView = imageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        // 5. 定义渲染通道：清空颜色/深度，结束后颜色图像进入显示布局。
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
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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

        // 6. 每张颜色图像配一个 framebuffer 和一个显示完成信号量。
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
        initPipeline();
        dirty = false;
        return true;
    }
    // 等待 GPU 停止使用资源，再释放与窗口尺寸相关的对象。
    void destroySwapchain()
    {
        if (!swapchain)
            return;
        vkDeviceWaitIdle(device);
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
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

    // 从磁盘读取构建时生成的 .spv，保留调试信息以便检查着色器。
    // Linux 下使用 /proc/self/exe 找到程序目录，再读取 shader/，不依赖工作目录。
    VkShaderModule shader(const char *name)
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

    // 按依赖的逆序释放资源：设备对象 -> 表面 -> 实例 -> X11 连接。
    void cleanup() noexcept
    {
        if (device)
        {
            vkDeviceWaitIdle(device);
            destroySwapchain();
            releaseResources();
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
    Renderer(SurfaceGeometry *owner, unsigned long windowId) : w(owner), nativeWindow(windowId)
    {
        try
        {
            initDevice();
            initResources();
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
    // 上传控制点和输入点；相机变化只更新 push constants，不重复上传。
    void initResources()
    {

        /*
         *   uniform buffer（200 个 vec4，每个 16 字节）：
         *
         *   [ 0 ... 99 ] 控制点  --> shader: control[100]
         *   [100 ...199] 输入点  --> shader: inputPoint[100]
         *
         *   每个点：(100*x, 100*z, 100*y, 1)。
         *   交换 y/z，把原始 Z 高度转换成显示坐标的 Y 高度。
         *   控制点的第四分量是权重；本例全部为 1。
         */

        // 1. 在 CPU 求插值控制点，再准备与着色器 std140 一致的数据。
        const auto data = nurbs::data(), control = nurbs::interpolate(data);
        float values[200][4];
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j)
            {
                const int index = i * 10 + j;
                for (int k = 0; k < 2; ++k)
                {
                    auto p = k == 0 ? control[i][j] : data[i][j];
                    values[index + k * 100][0] = float(p.x * 100);
                    values[index + k * 100][1] = float(p.z * 100);
                    values[index + k * 100][2] = float(p.y * 100);
                    values[index + k * 100][3] = 1;
                }
            }

        // 2. 创建 uniform buffer，分配 CPU 可见且一致的内存并上传。
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bc.size = sizeof(values);
        bc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        check(vkCreateBuffer(device, &bc, nullptr, &buffer), "Create buffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        const auto type =
            memoryType(requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &alloc, nullptr, &memory), "Allocate buffer");
        check(vkBindBufferMemory(device, buffer, memory, 0), "Bind buffer");
        void *mapped;
        check(vkMapMemory(device, memory, 0, sizeof(values), 0, &mapped), "Map buffer");
        std::memcpy(mapped, values, sizeof(values));
        vkUnmapMemory(device, memory);

        // 3. 描述符 binding=0 指向这个 buffer，仅供顶点着色器读取。
        // shader comment Surface
        // layout(binding = 0, std140) uniform Surface
        // {
        //     vec4 control[100]; // xyz in world coordinates, w is the rational weight
        //     vec4 inputPoint[100];
        // } surface;
        // The first field below declares binding 0 in descriptor set 0.
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                             VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dc.bindingCount = 1;
        dc.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device, &dc, nullptr, &descriptors),
              "Create descriptors");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets = 1;
        pc.poolSizeCount = 1;
        pc.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pc, nullptr, &pool), "Create pool");
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        da.descriptorPool = pool;
        da.descriptorSetCount = 1;
        da.pSetLayouts = &descriptors;
        check(vkAllocateDescriptorSets(device, &da, &set), "Allocate descriptors");

        // 分配描述符本身不建立数据关联，还需要写入 buffer 的范围。
        VkDescriptorBufferInfo bi{buffer, 0, sizeof(values)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        // shader comment begin Surface 
        // layout(binding = 0, std140) uniform Surface
        // {
        //     vec4 control[100]; // xyz in world coordinates, w is the rational weight
        //     vec4 inputPoint[100];
        // } surface;
        // shader comment end Surface 
        write.dstBinding = 0; // shader comment Surface: connect buffer to binding 0.
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        // 4. 每帧直接传递相机矩阵和显示模式：64 字节矩阵 + 模式及填充，共 80 字节。
        // shader comment View
        // layout(push_constant) uniform View
        // {
        //     mat4 mvp;
        //     int mode;
        // } view;
        // Push constants use a byte range and shader stage, with no descriptor binding.
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 80};
        VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lc.setLayoutCount = 1;
        lc.pSetLayouts = &descriptors;
        lc.pushConstantRangeCount = 1;
        lc.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device, &lc, nullptr, &layout), "Create pipeline layout");
    }
    // 组合顶点/片元着色器，使用线段拓扑和深度测试绘制曲面网格。
    void initPipeline()
    {

        /*
         *   gl_VertexIndex --> nurbs.vert --> 裁剪坐标 / 颜色
         *                                          |
         *                                          v
         *   framebuffer <-- nurbs.frag <-- 线段光栅化 / 深度测试
         *
         *   顶点由着色器按编号生成，因此不设置 vertex buffer 或顶点属性。
         *   每两个顶点组成一条线段；viewport/scissor 在每帧动态设置。
         */
        // shader comment color
        // layout(location = 0) out vec3 color;
        // Matches nurbs.frag: layout(location = 0) in vec3 color;
        // The pipeline connects these shader interfaces by location automatically.
        // No descriptor binding or CPU buffer is needed for this varying.
        VkShaderModule vert = shader("nurbs.vert.spv"), frag = shader("nurbs.frag.spv");
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
        ds.depthWriteEnable = VK_TRUE;
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
        ci.layout = layout;
        ci.renderPass = renderPass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline),
              "Create graphics pipeline");
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
    }

    // 释放不随窗口尺寸变化的控制点、描述符和管线布局资源。
    void releaseResources()
    {
        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyDescriptorPool(device, pool, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
    }
    // 一帧：等待上一帧 -> 获取图像 -> 记录命令 -> 提交 -> 显示。
    void draw()
    {
        /*
         *   CPU 等 submitted --> 获取 image（acquired）
         *                              |
         *                              v
         *   记录 command --> GPU 等 acquired --> 绘制 --> rendered[image]
         *                                                      |
         *                                                      v
         *                                            显示队列等待后呈现
         *
         *   submitted 保护命令缓冲和共享深度图像的复用。
         *   rendered 按图像分配，避免显示引擎仍在等待时再次使用它。
         */

        // 1. 检查尺寸并等待上一帧完成，然后按需重建交换链。
        const int width = w->m_width, height = w->m_height;
        if (width <= 0 || height <= 0)
        {
            w->requestUpdate();
            return;
        }
        check(vkWaitForFences(device, 1, &submitted, VK_TRUE, UINT64_MAX),
              "Wait for submitted frame");
        if (dirty)
        {
            destroySwapchain();
            if (!createSwapchain())
                return;
        }

        // 2. 获取显示图像；尺寸变化导致 OUT_OF_DATE 时请求下一次重建。
        uint32_t image = 0;
        VkResult result =
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &image);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            dirty = true;
            w->requestUpdate();
            return;
        }
        if (result != VK_SUBOPTIMAL_KHR)
            check(result, "Acquire swap chain image");
        bool recreate = result == VK_SUBOPTIMAL_KHR;

        // 3. 开始记录命令，设置清屏、管线、描述符和窗口像素范围。
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
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        // shader comment Surface: bind set 0, containing the uniform buffer at binding 0.
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0,
                                nullptr);
        VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, begin.renderArea.extent};
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);

        // 4. 计算相机变换；push constants 为每次绘制指定显示模式。
        const auto mvp = viewProjection(w->spin, w->tilt, w->distance,
                                        float(extent.width) / float(extent.height));
        struct Push
        {
            float matrix[16];
            int mode;
            int padding[3]{};
        } push{};
        std::memcpy(push.matrix, mvp.data(), 64);

        // mode=0：40400 个曲面顶点；mode=1/2：各 600 个十字标记顶点。
        const bool visible[] = {w->gridVisible, w->pointsVisible, w->controlPointsVisible};
        for (int mode = 0; mode < 3; ++mode)
            if (visible[mode])
            {
                push.mode = mode;

                // shader comment Surface
                // layout(binding = 0, std140) uniform Surface
                // {
                //     vec4 control[100]; // xyz in world coordinates, w is the rational weight
                //     vec4 inputPoint[100];
                // } surface;
                vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                
                vkCmdDraw(cb, mode == 0 ? 40400 : 600, 1, 0, 0);
            }
        vkCmdEndRenderPass(cb);
        check(vkEndCommandBuffer(cb), "End command buffer");

        // 5. 提交命令：等待图像可用，完成后通知显示队列并标记 fence。
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

        // 6. 显示当前图像；显示阶段也可能发现交换链过期。
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
            w->requestUpdate();
        }
    }
};

SurfaceGeometry::SurfaceGeometry() = default;
SurfaceGeometry::~SurfaceGeometry() = default;

// Qt 窗口首次可见时传入原生 ID；同一窗口只初始化一次。
void SurfaceGeometry::initialize(std::uintptr_t nativeWindow)
{
    if (!m_renderer)
        m_renderer = std::make_unique<Renderer>(this, static_cast<unsigned long>(nativeWindow));
}

// Qt 销毁原生窗口前调用，确保 Vulkan 不再引用该窗口。
void SurfaceGeometry::release()
{
    m_renderer.reset();
}

// 接收实际像素尺寸；只有尺寸变化才触发交换链重建。
void SurfaceGeometry::setFramebufferSize(int width, int height)
{
    if (width == m_width && height == m_height)
        return;
    m_width = width;
    m_height = height;
    if (m_renderer)
        m_renderer->resize();
    requestUpdate();
}

// 控件改变参数时设置绘制标记，由 Qt 的窗口事件调用 render()。
void SurfaceGeometry::requestUpdate()
{
    m_updateRequested = true;
}

// 消费绘制请求；draw() 遇到可恢复状态时可以再次设置标记。
void SurfaceGeometry::render()
{
    if (!m_renderer || !m_updateRequested)
        return;
    m_updateRequested = false;
    m_renderer->draw();
}

// CPU 插值误差供 UI 显示；它不包含着色器单精度计算的误差。
double SurfaceGeometry::interpolationError() const
{
    const auto data = nurbs::data();
    return nurbs::error(data, nurbs::interpolate(data));
}

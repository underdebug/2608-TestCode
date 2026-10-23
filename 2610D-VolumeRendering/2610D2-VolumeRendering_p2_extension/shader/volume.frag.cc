#include <cstdio>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>

// 引入 CPU 端着色器兼容接口和体绘制片元着色器。
#include "volume.frag"

// 对应 main.cpp 默认的 1100×800 窗口，减去宽 280 像素的控制面板。
// 可通过命令行参数指定渲染宽度和高度。
int viewportWidth = 820;
int viewportHeight = 800;

// 记录已渲染像素数，保存每个像素的颜色和深度。
int renderedPixels = 0;
std::vector<vec4> framebuffer;
std::vector<float> depthbuffer;

// 在此设置断点，打开 mem/plot_frag.py 并按 F8 显示渲染完成的图像。
void frameReady() { std::puts("Frame ready: open mem/plot_frag.py and press F8."); }

int main(int argc, char **argv)
{
    // 根据源文件路径定位项目根目录，准备 CPU 体数据缓存目录。
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto dataDirectory = root / "mem/data";
    std::filesystem::create_directories(dataDirectory);

    // 缓存不存在时，启动主程序，将 DICOM 数据导出为二进制体数据。
    auto cache = dataDirectory / "cpu-volume.bin";
    if (!std::filesystem::exists(cache)) {
        const auto program = (root / "build/test").string();
        const char *dicom = std::getenv("DICOM_DIRECTORY");
        if (!dicom) dicom = "/home/roots/develop/data/DICOM";

        // 子进程执行导出程序，父进程等待导出完成。
        const pid_t child = fork();
        if (child < 0) throw std::runtime_error("Cannot start DICOM exporter");
        if (child == 0) {
            execl(program.c_str(), program.c_str(), dicom, "--export-cpu", cache.c_str(), nullptr);
            // 仅在启动导出程序失败时执行，直接结束子进程。
            _exit(127);
        }

        // 检查子进程是否正常结束，以及导出操作是否成功。
        int status = 0;
        if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status))
            throw std::runtime_error("Build the CMake application first; DICOM export failed");
    }

    // 文件头依次保存体素维度和包围盒半尺寸，每项占 16 字节。
    std::ifstream file(cache, std::ios::binary);
    file.read(reinterpret_cast<char *>(&view.dimensions), 16);
    file.read(reinterpret_cast<char *>(&view.halfSize), 16);
    if (!file || view.dimensions.x <= 0 || view.dimensions.y <= 0 || view.dimensions.z <= 0)
        throw std::runtime_error("Invalid CPU volume header");

    // 每个 32 位整数存放两个 16 位体素，向上取整分配存储空间。
    size_t count = size_t(view.dimensions.x) * view.dimensions.y * view.dimensions.z;
    voxels.resize((count + 1) / 2);
    file.read(reinterpret_cast<char *>(voxels.data()), voxels.size() * 4);
    if (!file) throw std::runtime_error("Incomplete CPU volume");

    // 读取可选的渲染尺寸参数，并限制有效范围。
    if (argc > 2) {
        viewportWidth = std::stoi(argv[1]);
        viewportHeight = std::stoi(argv[2]);
    }
    if (viewportWidth < 1 || viewportHeight < 1 || viewportWidth > 4096 || viewportHeight > 4096)
        throw std::runtime_error("Viewport dimensions must be between 1 and 4096");

    // 使用与 VulkanWindow 相同的正交相机设置（旋转角度为 25° 和 55°）。
    float halfHeight = 450.0f * std::tan(3.14159265f / 8.0f);
    float halfWidth = halfHeight * float(viewportWidth) / viewportHeight;

    // 使用右手坐标系，裁剪空间深度范围为 [0, 1]；交换上下边界以翻转 Y 轴。
    mat4 projection = glm::orthoRH_ZO(-halfWidth, halfWidth, halfHeight, -halfHeight, 1.0f, 20000.0f);
    vec3 screen = vec3(projection * vec4(halfWidth, halfHeight, 0.0f, 1.0f));

    // 先平移相机，再依次应用绕 X 轴和 Y 轴的旋转。
    mat4 camera = glm::translate(mat4(1.0f), vec3(0, 0, -10000));
    // camera = glm::rotate(camera, glm::radians(55.0f), vec3(1, 0, 0));
    // camera = glm::rotate(camera, glm::radians(25.0f), vec3(0, 1, 0));

    mat4 camera2 = glm::lookAt(
        vec3(0.0f, 0.0f, 10000.0f), // Eye
        vec3(0.0f, 0.0f, 0.0f),     // Target
        vec3(0.0f, 1.0f, 0.0f));    // Up

    // 逆矩阵将裁剪空间坐标还原到体数据空间，用于生成采样光线。
    view.inverseMatrix = glm::inverse(projection * camera);

    // 依次设置窗位、窗宽、不透明度倍率和血管模式开关。
    view.transfer = vec4(275.0f, 350.0f, 1.0f, 1.0f);

    // 若存在主程序 recordScene() 保存的视图快照，则读取并使用该快照。
    const auto snapshot = dataDirectory / "cpu-view.bin";
    if (std::filesystem::exists(snapshot)) {
        // 确保视图结构大小与主程序写入的二进制布局一致。
        static_assert(sizeof(view) == 112);
        std::ifstream input(snapshot, std::ios::binary);
        input.read(reinterpret_cast<char *>(&view), sizeof(view));

        // 视图参数之后保存视口宽度和高度；快照尺寸优先于命令行参数。
        uint32_t extent[2]{};
        input.read(reinterpret_cast<char *>(extent), sizeof(extent));
        if (!input || extent[0] < 1 || extent[1] < 1 || extent[0] > 4096 || extent[1] > 4096)
            throw std::runtime_error("Invalid captured CPU view");

        // 检查快照中的体素总数是否与已加载的体数据一致。
        if (size_t(view.dimensions.x) * view.dimensions.y * view.dimensions.z != count)
            throw std::runtime_error("Captured view does not match CPU volume");

        viewportWidth = int(extent[0]);
        viewportHeight = int(extent[1]);
    }

    // 初始化颜色为黑色、不透明，深度为最远值。
    framebuffer.assign(size_t(viewportWidth) * viewportHeight, vec4(0, 0, 0, 1));
    depthbuffer.assign(framebuffer.size(), 1.0f);

    // 从视口中心向外逐圈绘制，圈数取水平和垂直半尺寸的较大值。
    int x0 = viewportWidth / 2;
    int y0 = viewportHeight / 2;
    int rounds = std::max(x0, y0);

    // 模拟单个片元的执行：设置输入、运行着色器并保存输出。
    auto renderPixel = [&](int x, int y) {
        // 外圈可能超出视口范围，直接跳过这些像素。
        if (x < 0 || x >= viewportWidth || y < 0 || y >= viewportHeight)
            return;

        // 使用像素中心坐标，将视口坐标映射到 [-1, 1]。
        ndc = vec2(2.0f * (x + 0.5f) / viewportWidth - 1.0f,
                    2.0f * (y + 0.5f) / viewportHeight - 1.0f);

        color = vec4(0, 0, 0, 1); // 被丢弃的片元保留此背景色。
        gl_FragDepth = 1.0f;

        // 执行 volume.frag 中的片元着色器。
        shader();

        // 按行优先顺序，将二维像素坐标转换为一维缓冲区下标。
        size_t pixel = size_t(y) * viewportWidth + x;
        framebuffer[pixel] = color;
        depthbuffer[pixel] = gl_FragDepth;
        ++renderedPixels;
    };

    // 每一圈绘制一个正方形边框，逐渐覆盖整个视口。
    for (int round = 0; round <= rounds; ++round) {
        int xstart = x0 - round;
        int xend = x0 + round;
        int ystart = y0 - round;
        int yend = y0 + round;

        // 绘制上、下边缘，包含四个角点。
        for (int x = xstart; x <= xend; ++x) {
            renderPixel(x, ystart);
            if (round > 0) renderPixel(x, yend);
        }

        // 绘制左、右边缘，跳过已经绘制的角点。
        for (int y = ystart + 1; y < yend; ++y) {
            renderPixel(xstart, y);
            renderPixel(xend, y);
        }

        // 原地刷新进度，便于观察 CPU 渲染过程。
        std::printf("\rRendering %d/%zu pixels", renderedPixels, framebuffer.size());
        std::fflush(stdout);
    }

    std::puts("");

    // 将 RGBA 浮点颜色缓冲区写入二进制文件，供后续读取和显示。
    std::ofstream image(root / "build/frag.rgba", std::ios::binary);
    image.write(reinterpret_cast<const char *>(framebuffer.data()), framebuffer.size() * sizeof(vec4));
    if (!image) throw std::runtime_error("Cannot write framebuffer");

    // 到达调试断点位置，此时整帧颜色和深度缓冲区均已就绪。
    frameReady();

    return 0;
}

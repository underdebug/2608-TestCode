#include <cstdio>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include "shader.h"
#include "volume.frag"

// Debugger-visible full-frame output. Increase these for a finer CPU image.
int viewportWidth = 128;
int viewportHeight = 128;
int renderedPixels = 0;
std::vector<vec4> framebuffer;
std::vector<float> depthbuffer;

// Put a breakpoint here to plot the completed frame with mem/plot_frag.py (F8).
void frameReady() { std::puts("Frame ready: open mem/plot_frag.py and press F8."); }

int main(int argc, char **argv)
{
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    auto cache = root / "build/cpu-volume.bin";
    if (!std::filesystem::exists(cache)) {
        const auto program = (root / "build/test").string();
        const char *dicom = std::getenv("DICOM_DIRECTORY");
        if (!dicom) dicom = "/home/roots/develop/data/DICOM";
        const pid_t child = fork();
        if (child < 0) throw std::runtime_error("Cannot start DICOM exporter");
        if (child == 0) {
            execl(program.c_str(), program.c_str(), dicom, "--export-cpu", cache.c_str(), nullptr);
            _exit(127);
        }
        int status = 0;
        if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status))
            throw std::runtime_error("Build the CMake application first; DICOM export failed");
    }
    std::ifstream file(cache, std::ios::binary);
    file.read(reinterpret_cast<char *>(&view.dimensions), 16);
    file.read(reinterpret_cast<char *>(&view.halfSize), 16);
    if (!file || view.dimensions.x <= 0 || view.dimensions.y <= 0 || view.dimensions.z <= 0)
        throw std::runtime_error("Invalid CPU volume header");
    size_t count = size_t(view.dimensions.x) * view.dimensions.y * view.dimensions.z;
    voxels.resize((count + 1) / 2);
    file.read(reinterpret_cast<char *>(voxels.data()), voxels.size() * 4);
    if (!file) throw std::runtime_error("Incomplete CPU volume");
    if (argc > 2) {
        viewportWidth = std::stoi(argv[1]);
        viewportHeight = std::stoi(argv[2]);
    }
    if (viewportWidth < 1 || viewportHeight < 1 || viewportWidth > 4096 || viewportHeight > 4096)
        throw std::runtime_error("Viewport dimensions must be between 1 and 4096");
    // Same orthographic camera convention as VulkanWindow (25/55 degrees).
    float halfHeight = 450.0f * std::tan(3.14159265f / 8.0f);
    float halfWidth = halfHeight * float(viewportWidth) / viewportHeight;
    mat4 projection = glm::orthoRH_ZO(-halfWidth, halfWidth, halfHeight, -halfHeight, 1.0f, 20000.0f);
    mat4 camera = glm::translate(mat4(1.0f), vec3(0, 0, -10000));
    camera = glm::rotate(camera, glm::radians(55.0f), vec3(1, 0, 0));
    camera = glm::rotate(camera, glm::radians(25.0f), vec3(0, 1, 0));
    view.inverseMatrix = glm::inverse(projection * camera);
    view.transfer = vec4(275.0f, 350.0f, 1.0f, 1.0f);
    framebuffer.assign(size_t(viewportWidth) * viewportHeight, vec4(0, 0, 0, 1));
    depthbuffer.assign(framebuffer.size(), 1.0f);
    for (int y = 0; y < viewportHeight; ++y) {
        for (int x = 0; x < viewportWidth; ++x) {
            ndc = vec2(2.0f * (x + 0.5f) / viewportWidth - 1.0f,
                        2.0f * (y + 0.5f) / viewportHeight - 1.0f);
            color = vec4(0, 0, 0, 1); // Background for discarded fragments.
            gl_FragDepth = 1.0f;
            shader();
            size_t pixel = size_t(y) * viewportWidth + x;
            framebuffer[pixel] = color;
            depthbuffer[pixel] = gl_FragDepth;
            ++renderedPixels;
        }
        std::printf("\rRendering %d/%d rows", y + 1, viewportHeight);
        std::fflush(stdout);
    }
    std::puts("");
    std::ofstream image(root / "build/frag.rgba", std::ios::binary);
    image.write(reinterpret_cast<const char *>(framebuffer.data()), framebuffer.size() * sizeof(vec4));
    if (!image) throw std::runtime_error("Cannot write framebuffer");
    frameReady();

    return 0;
}

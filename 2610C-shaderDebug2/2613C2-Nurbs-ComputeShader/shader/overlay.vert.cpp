#include <cstdio>
#include <cmath>
#include <limits>
#include "shader.h"
#include "overlay.vert"

struct VertexOutput
{
    vec4 position;
    vec3 color;
};

int main()
{
    // Match the push constants and draw counts in VulkanWindow.cpp.
    // Inspect outputVertices or break in shader()/drawRuler() when debugging.
    std::vector<VertexOutput> outputVertices;
    for (int mode : {3, 4})
    for (float step : {100.0f, 1.0f, 0.1f, 0.01f, 0.001f})
    {
        view = {};
        view.mode = mode;
        view.viewportWidth = 1280;
        view.viewportHeight = 720;
        if (mode == 3)
        {
            view.mvp[0] = vec4(1280, 720, step / 100.0f, step);
            view.mvp[1] = vec4(step * 0.25f, -step * 0.5f, 0, 0);
        }
        else
        {
            // Orthographic transform with all three axes visible.
            view.mvp = mat4(1.0f);
            view.mvp[0] = vec4(0.3f, 0.1f, 0, 0);
            view.mvp[1] = vec4(-0.1f, -0.3f, 0, 0);
            view.mvp[2] = vec4(0, 0.2f, 0.1f, 0);
        }
        const int vertexCount = mode == 3 ? 2 * (2 + 2 * 16 * 57) : 30;
        outputVertices.resize(vertexCount);
        int visibleVertices = 0;
        for (gl_VertexIndex = 0; gl_VertexIndex < vertexCount; ++gl_VertexIndex)
        {
            gl_Position = vec4(std::numeric_limits<float>::quiet_NaN());
            color = vec3(std::numeric_limits<float>::quiet_NaN());
            shader();
            outputVertices[gl_VertexIndex] = {gl_Position, color};
            for (int component = 0; component < 4; ++component)
                if (!std::isfinite(gl_Position[component]))
                {
                    std::fprintf(stderr, "Invalid position: mode %d, vertex %d\n", mode, gl_VertexIndex);
                    return 1;
                }
            for (int component = 0; component < 3; ++component)
                if (!std::isfinite(color[component]))
                {
                    std::fprintf(stderr, "Invalid color: mode %d, vertex %d\n", mode, gl_VertexIndex);
                    return 1;
                }
            if (gl_Position != vec4(2, 2, 0, 1)) ++visibleVertices;
        }
        if (visibleVertices == 0) return 1;
        std::printf("mode %d, step %g: %d vertices, %d visible\n",
                    mode, step, vertexCount, visibleVertices);
        if (mode == 4) break;
    }

    return 0;
}

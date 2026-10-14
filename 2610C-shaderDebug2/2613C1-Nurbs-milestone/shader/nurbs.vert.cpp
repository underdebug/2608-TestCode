#include <cstdio>
#include <vector>
#include "shader.h"
#include "nurbs.vert"

int main()
{
    surface = {};
    {
        FILE* file = std::fopen("mem/data/surface.dat", "rb");
        if (!file) { std::perror("mem/data/surface.dat"); return 1; }
        const auto bytes = std::fread(&surface, 1, sizeof(surface), file);
        std::fclose(file);
        if (bytes != sizeof(surface)) { std::fprintf(stderr, "Incomplete surface data\n"); return 1; }
    }

    view = {};
    {
        FILE* file = std::fopen("mem/data/push.dat", "rb");
        if (!file) { std::perror("mem/data/push.dat"); return 1; }
        const auto bytes = std::fread(&view, 1, sizeof(view), file);
        std::fclose(file);
        if (bytes != sizeof(view)) { std::fprintf(stderr, "Incomplete push data\n"); return 1; }
    }

    if (view.mode < 0 || view.mode > 2)
    {
        std::fprintf(stderr, "Invalid shader mode: %d\n", view.mode);
        return 1;
    }

    // Match the vertex count passed to vkCmdDraw in SurfaceGeometry.cpp.
    const int shaderProcessCount = view.mode == 0 ? 40400 : 600;
    std::vector<vec4> outputPositions(shaderProcessCount);
    for (gl_VertexIndex = 0; gl_VertexIndex < shaderProcessCount; ++gl_VertexIndex)
    {
        shader();
        outputPositions[gl_VertexIndex] = gl_Position;
    }
    std::printf("Shader process count: %d\n", shaderProcessCount);
    return 0;
}

/*

import numpy as np
from pathlib import Path

data_path = Path(mem.__code__.co_filename).resolve().parent / 'data'
data_path.mkdir(parents=True, exist_ok=True)

#LINE 635
size = mem('sizeof(values)')
surface = mem('values', 'uc', int(size))
np.asarray(surface, dtype=np.uint8).tofile(data_path / 'surface.dat')
_surface = surface.view(np.float32)
print('_surface', _surface)



#LINE 902
size = mem('sizeof(push)')
push = mem('push', 'uc', int(size))
np.asarray(push, dtype=np.uint8).tofile(data_path / 'push.dat')
_push = push.view(np.float32)
print('_push', _push)

*/

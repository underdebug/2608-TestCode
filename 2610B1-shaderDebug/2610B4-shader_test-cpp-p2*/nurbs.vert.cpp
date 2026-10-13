#include <stdio.h>
#include "shader.h"
#include "nurbs.vert"

int main() {
    for (int i = 0; i < 100; ++i) {
        surface.control[i] = vec4(2, 3, 4, 1);
        surface.inputPoint[i] = surface.control[i];
    }
    view.mvp = mat4(1);
    view.mode = 0;
    gl_VertexIndex = 0;
    shader();
    printf("gl_Position = (%g, %g, %g, %g)\n",
           gl_Position.x, gl_Position.y, gl_Position.z, gl_Position.w);
    return 0;
}

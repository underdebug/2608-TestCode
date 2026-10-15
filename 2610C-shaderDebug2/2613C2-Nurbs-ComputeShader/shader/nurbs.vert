// GLSL version 450 is selected by CMake.
layout(binding = 1, std430) readonly buffer DisplayPoints
{
    vec4 position[43472];
} points;

layout(push_constant) uniform View
{
    mat4 mvp;
    int mode;
} view;

layout(location = 0) out vec3 color;

void main()
{
    int offset = view.mode == 0 ? 0 : (view.mode == 1 ? 40400 : 41936);
    color = view.mode == 0 ? vec3(0.22, 0.73, 0.94) :
            (view.mode == 1 ? vec3(1, 0.40, 0.31) : vec3(0.40, 0.90, 0.50));
    gl_Position = view.mvp * points.position[offset + gl_VertexIndex];
}

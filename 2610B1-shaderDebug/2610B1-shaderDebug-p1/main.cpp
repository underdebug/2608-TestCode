
#include <cstdio>
#include <array>
#include <cmath>
#include <algorithm>
#include <Eigen/Dense>

#include "shaders/shader.h"
#include "shaders/nurbs.vert"

using Grid = std::array<std::array<glm::dvec3, 10>, 10>;

inline Grid data()
{
    Grid d;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
        {
            double x = 2. * i / 9 - 1, y = 2. * j / 9 - 1;
            d[i][j] = {x, y, 0.35 * std::sin(3 * x) * std::cos(3 * y) + 0.15 * x * y};
        }
    return d;
}

// Interpolate D = A C A^T at parameters i / 9, using the shader's basis.
Grid controlPoints(const Grid& d)
{
    Eigen::Matrix<double, 10, 10> a;
    for (int i = 0; i < 10; ++i)
    {
        float b[13];
        basis(float(i) / 9, b);
        for (int j = 0; j < 10; ++j)
            a(i, j) = b[j];
    }
    const auto solver = a.partialPivLu();
    Grid controls;
    for (int axis = 0; axis < 3; ++axis)
    {
        Eigen::Matrix<double, 10, 10> coordinates;
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j)
                coordinates(i, j) = d[i][j][axis];
        const Eigen::Matrix<double, 10, 10> intermediate = solver.solve(coordinates);
        const Eigen::Matrix<double, 10, 10> c = solver.solve(intermediate.transpose()).transpose();
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j)
                controls[i][j][axis] = c(i, j);
    }
    return controls;
}

int main(int argc, char *argv[])
{
    freopen("log.txt", "w", stdout);
    setvbuf(stdout, nullptr, _IONBF, 0);
    // tail -f log.txt

    const Grid d = data();
    const Grid controls = controlPoints(d);
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
        {
            surface.inputPoint[i * 10 + j] = vec4(vec3(d[i][j]), 1.0f);
            surface.control[i * 10 + j] = vec4(vec3(controls[i][j]), 1.0f);
        }
    
    view.mvp = mat4(1.0f);
    view.mode = 3;
    double maxError = 0;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
            maxError = std::max(maxError, glm::length(
                glm::dvec3(evaluate(vec2(float(i) / 9, float(j) / 9))) - d[i][j]));
    std::fprintf(stderr, "Maximum interpolation error: %g\n", maxError);
    if (!std::isfinite(maxError) || maxError > 1e-5)
        return 1;

    // Identity MVP makes the shader output equal to world coordinates.
    std::array<std::array<vec3, surfaceRowSize>, surfaceRowSize> surfacePoints;
    for (gl_VertexIndex = 0; gl_VertexIndex < surfaceVertexCount; ++gl_VertexIndex)
    {
        _main();
        const vec3 p = gl_Position.xyz;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            return 1;
        // Store as [u][v], matching the input grid's [x][y] indices.
        surfacePoints[gl_VertexIndex % surfaceRowSize][gl_VertexIndex / surfaceRowSize] = p;
        const vec2 uv = surfaceUV(gl_VertexIndex);
        std::printf("u=%g v=%g Position: %g %g %g\n", uv.x, uv.y, p.x, p.y, p.z);
    }

    return 0;
}

#include <cmath>
#include <iostream>
#include <stdexcept>
#include "shader_cpu.hpp"
#include "nurbs.vert"

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a-b) < 0.0001f; }
int main() {
    int a = 1;
    int b = 2;
    // A constant control net must evaluate to the same point for any positive weights.
    for (int i = 0; i < 100; ++i) {
        surface.control[i] = {2, 3, 4, float(i % 7 + 1)};
        surface.inputPoint[i] = {10, 20, 30, 1};
    }
    for (float u : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}) {
        float b[13];
        basis(u, b);
        float sum = 0;
        for (int i = 0; i < 10; ++i) sum += b[i];
        check(near(sum, 1), "Basis must sum to one");
        for (float v : {0.0f, 0.5f, 1.0f}) {
            vec3 p = evaluate(vec2(u, v));
            check(near(p.x, 2) && near(p.y, 3) && near(p.z, 4), "Constant surface evaluation failed");
        }
    }
    surface.control[0] = {1, 2, 3, 1};
    surface.control[99] = {7, 8, 9, 2};
    check(near(evaluate(vec2(0, 0)).x, 1), "Start corner failed");
    check(near(evaluate(vec2(1, 1)).x, 7), "End corner failed");
    view.mvp = mat4(1);
    view.mode = 0;
    for (int id = 0; id < 40000; ++id) {
        gl_VertexIndex = id;
        shader();
        check(std::isfinite(gl_Position.x) && std::isfinite(gl_Position.y) &&
              std::isfinite(gl_Position.z) && near(gl_Position.w, 1), "Surface vertex invalid");
    }
    for (int mode : {1, 2}) {
        view.mode = mode;
        for (int id = 0; id < 600; ++id) {
            gl_VertexIndex = id;
            shader();
            vec3 expected = mode == 1 ? surface.inputPoint[id/6].xyz() : surface.control[id/6].xyz();
            expected[(id%6)/2] += (id%2 == 0 ? -1 : 1) * (mode == 1 ? 1.8f : 2.5f);
            check(near(gl_Position.x, expected.x) && near(gl_Position.y, expected.y) &&
                  near(gl_Position.z, expected.z), "Point marker failed");
        }
    }
    std::cout << "NURBS CPU checks passed: basis, rational evaluation, corners, 40000 surface vertices, 1200 marker vertices\n";
}

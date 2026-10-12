#include <iostream>

#include "shader_cpu.hpp"
#include "example.vert"

int main() {
    const float input = 3.0f;
    const float result = square(input);
    inPosition = {1.0f, 2.0f, 3.0f};
    shader();
    std::cout << "square(" << input << ") = " << result << '\n';
    std::cout << "gl_Position = (" << gl_Position.x << ", " << gl_Position.y
              << ", " << gl_Position.z << ", " << gl_Position.w << ")\n";
    return result == 9.0f ? 0 : 1;
}

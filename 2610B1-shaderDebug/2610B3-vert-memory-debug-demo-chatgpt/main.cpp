#include <iostream>
#include "shader_cpu.hpp"

#include "shaders/example.vert"

int main() {
    const float input = 3.0f;
    const float result = square(input);
    shader();
    std::cout << "square(" << input << ") = " << result << '\n';
    return result == 9.0f ? 0 : 1;
}

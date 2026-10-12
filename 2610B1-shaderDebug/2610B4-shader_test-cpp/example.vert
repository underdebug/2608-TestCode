#version 450

float square(float x) {
    float result = x * x;
    return result;
}

layout(location = 0) in vec3 inPosition;

void main() {
    float scale = square(0.5);
    gl_Position = vec4(inPosition * scale, 1.0);
}
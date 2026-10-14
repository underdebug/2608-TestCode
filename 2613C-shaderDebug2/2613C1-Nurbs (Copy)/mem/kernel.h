#pragma once

// CPU equivalents for the GLSL operations exercised by the demos.
struct vec2 {
    float x{}, y{};
    vec2(float x, float y) : x(x), y(y) {}
    vec2 yx() const { return {y, x}; }
};
struct vec3 {
    float x{}, y{}, z{};
    vec3() = default;
    explicit vec3(float v) : x(v), y(v), z(v) {}
    vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
    vec3 operator/(float s) const { return {x/s, y/s, z/s}; }
    vec3& operator+=(vec3 v) { x+=v.x; y+=v.y; z+=v.z; return *this; }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};
struct vec4 {
    float x{}, y{}, z{}, w{};
    vec4() = default;
    vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    vec4(vec3 xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
    vec3 xyz() const { return {x, y, z}; }
};
struct mat4 {
    // Column-major storage, matching GLSL.
    float columns[4][4]{};
    explicit mat4(float diagonal = 1) {
        for (int i = 0; i < 4; ++i) columns[i][i] = diagonal;
    }
    vec4 operator*(vec4 v) const {
        float input[4] = {v.x, v.y, v.z, v.w};
        float output[4]{};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                output[row] += columns[col][row] * input[col];
        return {output[0], output[1], output[2], output[3]};
    }
};
inline vec4 gl_Position;
inline int gl_VertexIndex;

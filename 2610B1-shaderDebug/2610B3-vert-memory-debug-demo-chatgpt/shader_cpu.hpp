#pragma once

// CPU equivalents for the GLSL operations used by example.vert.
struct vec3 {
    float x{}, y{}, z{};

    vec3 operator*(float scale) const {
        return {x * scale, y * scale, z * scale};
    }
};

struct vec4 {
    float x{}, y{}, z{}, w{};

    vec4() = default;
    vec4(vec3 xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
};

inline vec4 gl_Position;

#pragma once

#ifndef GLM_FORCE_SWIZZLE
#define GLM_FORCE_SWIZZLE
#endif
#include <glm/glm.hpp>
#include <vector>
#include <cmath>

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using glm::clamp;
using glm::max;
using glm::min;
using std::abs;
using std::ceil;
using std::floor;
using std::log2;
using std::pow;
using std::round;

#define layout(...)
#define uniform struct
#define buffer struct
#define readonly
#define in
#define out
#define inout

#define xyz xyz()
#define xy xy()
#define yx yx()

inline int gl_VertexIndex = 0;
inline glm::uvec3 gl_GlobalInvocationID{0u};
inline vec4 gl_Position{0.0f};

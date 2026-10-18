#pragma once

#ifndef GLM_FORCE_SWIZZLE
#define GLM_FORCE_SWIZZLE
#endif
#include <glm/glm.hpp>
#include <vector>

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;

#define layout(...)
#define uniform struct
#define buffer struct
#define readonly
#define in
#define out
#define inout

#define xyz xyz()
#define yx yx()

inline int gl_VertexIndex = 0;
inline glm::uvec3 gl_GlobalInvocationID{0u};
inline vec4 gl_Position{0.0f};

#pragma once
#define _main _main

#ifndef GLM_FORCE_SWIZZLE
#define GLM_FORCE_SWIZZLE
#endif
#include <glm/glm.hpp>

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;

#define layout(...)
#define uniform struct
#define in
#define out

#define yx yx()
#define xyz xyz()

inline int gl_VertexIndex = 0;
inline vec4 gl_Position{0.0f};


#ifndef _main
#version 450
#define _main main
#endif

layout(location = 0) in vec3 color;
layout(location = 0) out vec4 fragColor;

void _main()
{
    fragColor = vec4(color, 1);
}

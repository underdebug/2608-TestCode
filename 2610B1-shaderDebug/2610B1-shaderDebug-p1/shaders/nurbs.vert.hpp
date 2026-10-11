
//ifndef _main
//version 450
//define _main main
//endif

layout(binding = 0, std140) uniform Surface
{
    vec4 control[100]; // xyz in world coordinates, w is the rational weight
    vec4 inputPoint[100];
} surface;

layout(push_constant) uniform View
{
    mat4 mvp;
    int mode; // 0: wireframe, 1: input markers, 2: control markers, 3: surface samples
} view;

layout(location = 0) out vec3 color;

const int surfaceResolution = 100;
const int surfaceRowSize = surfaceResolution + 1;
const int surfaceVertexCount = surfaceRowSize * surfaceRowSize;

// V is the outer index; U advances first along X. Include both endpoints.
vec2 surfaceUV(int id)
{
    float v = float(id / surfaceRowSize) / float(surfaceResolution);
    float u = float(id % surfaceRowSize) / float(surfaceResolution);
    return vec2(u, v);
}

const float knots[14] = {
    0, 0, 0, 0,
    2.0f / 9, 3.0f / 9, 4.0f / 9,
    5.0f / 9, 6.0f / 9, 7.0f / 9,
    1, 1, 1, 1
};

void basis(float t, out float b[13])
{
    for (int i = 0; i < 13; ++i)
        b[i] = (t >= knots[i] && t < knots[i + 1]) ? 1.0 : 0.0;

    if (t >= 1.0)
    {
        for (int i = 0; i < 13; ++i)
            b[i] = 0.0;
        b[9] = 1.0;
        return;
    }

    for (int d = 1; d <= 3; ++d)
        for (int i = 0; i < 13 - d; ++i)
        {
            float a = knots[i + d] - knots[i], c = knots[i + d + 1] - knots[i + 1];
            b[i] = (a > 0 ? (t - knots[i]) * b[i] / a : 0) +
                   (c > 0 ? (knots[i + d + 1] - t) * b[i + 1] / c : 0);
        }
}

vec3 evaluate(vec2 uv)
{
    float a[13], b[13];

    basis(uv.x, a);
    basis(uv.y, b);

    vec3 p = vec3(0);
    float denominator = 0;

    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
        {
            vec4 cp = surface.control[i * 10 + j];
            float w = a[i] * b[j] * cp.w;

            p += cp.xyz * w;
            denominator += w;
        }

    return p / denominator;
}

void _main()
{
    int id = gl_VertexIndex;
    vec3 p;

    if (view.mode == 3)
    {
        p = evaluate(surfaceUV(id));
        color = vec3(0.22, 0.73, 0.94);
    }
    else if (view.mode == 0)
    {
        int cell = id / 4, direction = (id % 4) / 2, endpoint = id % 2;

        vec2 uv = vec2(float(cell / 100) / 100.0, float(cell % 100 + endpoint) / 100.0);
        if (direction == 1)
            uv = uv.yx;

        p = evaluate(uv);
        color = vec3(0.22, 0.73, 0.94);
    }
    else
    {
        int point = id / 6, axis = (id % 6) / 2;

        p = view.mode == 1 ? surface.inputPoint[point].xyz : surface.control[point].xyz;
        p[axis] += ((id % 2) == 0 ? -1.0 : 1.0) * (view.mode == 1 ? 1.8 : 2.5);

        color = view.mode == 1 ? vec3(1, 0.40, 0.31) : vec3(0.40, 0.90, 0.50);
    }

    gl_Position = view.mvp * vec4(p, 1);
}

// Window rulers and coordinate axes.
layout(push_constant) uniform View
{
    mat4 mvp;
    int mode;
    int viewportWidth;
    int viewportHeight;
} view;

layout(location = 0) out vec3 color;

// Seven-segment numeric labels, generated as line vertices without a font texture.
const int digitMask[10] = int[](63, 6, 91, 79, 102, 109, 125, 7, 127, 111);
const vec2 segmentEnds[16] = vec2[](
    vec2(0,0),vec2(4,0), vec2(4,0),vec2(4,5),
    vec2(4,5),vec2(4,10), vec2(0,10),vec2(4,10),
    vec2(0,5),vec2(0,10), vec2(0,0),vec2(0,5),
    vec2(0,5),vec2(4,5), vec2(5,10),vec2(6,10));

void drawRuler()
{
    vec2 size = view.mvp[0].xy;
    float scale = view.mvp[0].z;
    float stepSize = view.mvp[0].w;
    vec2 pan = view.mvp[1].xy;
    int line = gl_VertexIndex / 2;
    int end = gl_VertexIndex % 2;
    vec2 p;
    color = vec3(1, 0.2, 0.2);
    if (line < 2)
    {
        p = line == 0 ? vec2(float(end) * size.x, size.y - 1)
                      : vec2(size.x - 1, float(end) * size.y);
    }
    else
    {
        int item = (line - 2) / 57;
        int axis = item / 16;
        int tick = item % 16;
        int part = (line - 2) % 57;
        float low = -size[axis] * scale * 0.5 - pan[axis];
        float mark = (ceil(low / stepSize) + float(tick)) * stepSize;
        float pos = (mark + pan[axis]) / scale + size[axis] * 0.5;
        if (axis == 1) pos = size.y - pos;
        if (pos < 1 || pos > size[axis] - 1)
        {
            gl_Position = vec4(2, 2, 0, 1);
            return;
        }
        if (part == 0)
            p = axis == 0 ? vec2(pos, size.y - 1 - float(end) * 6)
                          : vec2(size.x - 1 - float(end) * 6, pos);
        else
        {
            int character = (part - 1) / 8;
            int segment = (part - 1) % 8;
            int decimals = int(clamp(-floor(log2(stepSize) / log2(10.0)), 0.0, 3.0));
            int value = int(round(abs(mark) * pow(10.0, float(decimals))));
            int remaining = 6 - character;
            int divisor = int(round(pow(10.0, float(remaining))));
            int mask = character == 0 ? (mark < -stepSize * 0.5 ? 64 : 0)
                : ((value >= divisor || remaining <= decimals) ? digitMask[(value / divisor) % 10] : 0);
            bool visibleSegment = segment == 7 ? (character > 0 && decimals > 0 && remaining == decimals)
                                      : ((mask & (1 << segment)) != 0);
            if (!visibleSegment)
            {
                gl_Position = vec4(2, 2, 0, 1);
                return;
            }
            // Pack the sign next to the first digit instead of reserving
            // empty leading character slots for every label.
            bool negative = mark < -stepSize * 0.5;
            int leadingPower = max(decimals, int(floor(log2(float(max(value, 1))) / log2(10.0))));
            int firstDigit = 6 - min(leadingPower, 5);
            int glyph = character == 0 ? 0 : character - firstDigit + (negative ? 1 : 0);
            float labelWidth = float(7 - firstDigit + (negative ? 1 : 0)) * 7.0 - 3.0;
            vec2 origin = axis == 0 ? vec2(pos - labelWidth * 0.5, size.y - 19)
                                    : vec2(size.x - 9 - labelWidth, pos - 5);
            p = origin + vec2(float(glyph) * 7, 0) + segmentEnds[segment * 2 + end];
            color = vec3(0.95);
        }
    }
    gl_Position = vec4(p / size * 2.0 - 1.0, 0, 1);
}

void main()
{
    if (view.mode == 3)
    {
        drawRuler();
        return;
    }
    if (view.mode == 4)
    {
        int axis;
        vec3 p = vec3(0);
        vec2 labelOffset = vec2(0);
        if (gl_VertexIndex < 12)
        {
            axis = gl_VertexIndex / 4;
            p[axis] = gl_VertexIndex % 2 == 0 ? 0 : 1;
        }
        else
        {
            // Project only the anchor; glyph geometry stays in screen pixels.
            const vec2 glyph[18] = vec2[](
                vec2(0,0),vec2(8,12), vec2(0,12),vec2(8,0), vec2(0,0),vec2(0,0),
                vec2(0,12),vec2(4,6), vec2(8,12),vec2(4,6), vec2(4,6),vec2(4,0),
                vec2(0,12),vec2(8,12), vec2(8,12),vec2(0,0), vec2(0,0),vec2(8,0));
            int index = gl_VertexIndex - 12;
            axis = index / 6;
            p[axis] = 1;
            labelOffset = vec2(5, -5) + glyph[index] * vec2(1, -1);
        }
        color = axis == 0 ? vec3(1,0.2,0.2) : (axis == 1 ? vec3(0.2,1,0.2) : vec3(0.3,0.5,1));
        gl_Position = view.mvp * vec4(p, 1);
        vec2 viewportSize = vec2(view.viewportWidth, view.viewportHeight);
        if (gl_VertexIndex < 12)
        {
            // Two adjacent one-pixel strokes work without Vulkan wideLines support.
            vec2 direction = view.mvp[axis].xy * viewportSize;
            float projectedLength = length(direction);
            vec2 normal = projectedLength > 0.00001
                ? vec2(-direction.y, direction.x) / projectedLength : vec2(1, 0);
            float side = (gl_VertexIndex / 2) % 2 == 0 ? -0.5 : 0.5;
            gl_Position.xy += normal * side * 2.0 / viewportSize * gl_Position.w;
        }
        else
        {
            gl_Position.xy += labelOffset * 2.0 / viewportSize * gl_Position.w;
        }
        return;
    }
}

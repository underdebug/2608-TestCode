#version 450

// 屏幕归一化坐标输入，以及最终像素颜色输出。
layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 color;

// 通过只读存储缓冲区读取体素；CPU 回放时由编译工具转换为数组。
// 每个 32 位无符号整数打包两个 16 位有符号 CT 值。
layout(set = 0, binding = 0, std430) readonly buffer Volume
{
    uint voxels[];
};

layout(push_constant) uniform View
{
    mat4 inverseMatrix; // 将裁剪空间坐标变换到体数据空间。
    vec4 halfSize;      // xyz：体数据包围盒的半尺寸，单位为毫米。
    ivec4 dimensions;  // xyz：三个方向的体素数量。
    vec4 transfer;     // 窗位、窗宽、不透明度倍率、血管模式开关。
} view;

// 读取单个体素：限制索引范围，解包后恢复有符号 CT 值。
float voxel(ivec3 p)
{
    p = clamp(p, ivec3(0), view.dimensions.xyz - 1);
    int index = (p.z * view.dimensions.y + p.y) * view.dimensions.x + p.x;
    uint raw = (voxels[index / 2] >> ((index & 1) * 16)) & 65535u;

    return float(int(raw << 16) >> 16);
}

// 将毫米坐标映射到体素中心坐标，再对周围八个体素做三线性插值。
float sampleVolume(vec3 position)
{
    // 0   * 512  - 0.5 = -0.5
    // 0.5 * 512  - 0.5 = 255.5
    // 1   * 512  - 0.5 = 511.5

    // vec3 halfdimensions = vec3(view.dimensions.xyz) / 2 - 0.5;
    // vec3 p = (position / (2.0 * view.halfSize.xyz) * vec3(view.dimensions.xyz)) + halfdimensions

    vec3 p = (position / (2.0 * view.halfSize.xyz) + 0.5) * vec3(view.dimensions.xyz) - 0.5;

    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p);

    // 先沿 x 插值，再沿 y 合并，最后沿 z 混合前后两层。
    return mix(
        mix(
            mix(voxel(i), voxel(i + ivec3(1, 0, 0)), f.x),
            mix(voxel(i + ivec3(0, 1, 0)), voxel(i + ivec3(1, 1, 0)), f.x),
            f.y),
        mix(
            mix(voxel(i + ivec3(0, 0, 1)), voxel(i + ivec3(1, 0, 1)), f.x),
            mix(voxel(i + ivec3(0, 1, 1)), voxel(i + ivec3(1, 1, 1)), f.x),
            f.y),
        f.z);
}

// 用中心差分计算密度梯度，单位为 HU/mm。
// 按各轴实际体素间距计算，避免层厚不同导致法线方向失真。
vec3 volumeGradient(vec3 position)
{
    vec3 spacing = 2.0 * view.halfSize.xyz / vec3(view.dimensions.xyz);
    vec3 gradient;

    for (int axis = 0; axis < 3; ++axis)
    {
        vec3 offset = vec3(0);
        offset[axis] = spacing[axis];
        gradient[axis] = (sampleVolume(position + offset) -
                          sampleVolume(position - offset)) / (2.0 * spacing[axis]);
    }

    return gradient;
}

// 根据密度梯度估算法线，叠加环境光、漫反射和镜面高光。
vec3 shadeVolume(vec3 tissue, vec3 position, vec3 toEye, vec3 toLight)
{
    vec3 gradient = volumeGradient(position);
    float magnitude = length(gradient);

    // 均匀区域没有稳定法线，只保留基础亮度。
    if (magnitude < 1e-4)
    {
        return tissue * 0.65;
    }

    // 密度增大的方向通常指向组织内部，取反得到朝外的法线。
    vec3 normal = -gradient / magnitude;
    // 让法线朝向观察者，改善体绘制内部采样点的明暗表现。
    if (dot(normal, toEye) < 0.0)
    {
        normal = -normal;
    }

    // 半程向量用于计算 Blinn–Phong 高光。
    float diffuse = max(dot(normal, toLight), 0.0);
    vec3 halfway = normalize(toLight + toEye);
    float specular = diffuse > 0.0
        ? pow(max(dot(normal, halfway), 0.0), 40.0)
        : 0.0;

    // 抑制低梯度区域的高光，避免放大体数据噪声。
    specular *= smoothstep(1.0, 20.0, magnitude);

    return tissue * (0.55 + 0.65 * diffuse) + vec3(0.65 * specular);
}

void main()
{
    float inverseMatrixCol0Row1 = view.inverseMatrix[0][1]; // 0 col, 1 row

    // 当前使用正交投影：反变换近、远裁剪面上的点，构造采样光线。
    vec3 origin = (view.inverseMatrix * vec4(ndc, 0, 1)).xyz;
    vec3 end = (view.inverseMatrix * vec4(ndc, 1, 1)).xyz;
    vec3 direction = normalize(end - origin);

    // 光源固定在观察者右上方；相机旋转后仍在体数据空间计算光照。
    vec3 cameraRight = normalize(view.inverseMatrix[0].xyz);
    vec3 cameraUp = -normalize(view.inverseMatrix[1].xyz);
    vec3 toLight = normalize(-direction + 0.45 * cameraRight + 0.65 * cameraUp);

    // 用极小值替代接近零的方向分量，避免求倒数时除零。
    vec3 invDirection = 1.0 / mix(
        direction, vec3(1e-8), lessThan(abs(direction), vec3(1e-8)));
    // 分别计算光线与包围盒各轴平面的交点，求共同的进入和离开距离。
    vec3 a = (-view.halfSize.xyz - origin) * invDirection;
    vec3 b = (view.halfSize.xyz - origin) * invDirection;
    vec3 lo = min(a, b), hi = max(a, b);
    float enter = max(max(lo.x, lo.y), max(lo.z, 0.0));
    float leave = min(min(hi.x, hi.y), hi.z);

    // 光线没有穿过包围盒时，不写入颜色和深度。
    if (leave <= enter)
        discard;


    // 通常以最小体素间距的一半采样，以保留细小结构。
    // 对较长光线增大步长，将采样次数限制在约 2048 次以内。
    vec3 spacing = 2.0 * view.halfSize.xyz / vec3(view.dimensions.xyz);
    float stepSize = max(
        (leave - enter) / 2048.0,
        0.5 * min(min(spacing.x, spacing.y), spacing.z));

    // rgb 保存累计颜色，a 保存累计不透明度；firstDepth 记录可见组织深度。
    vec4 accumulated = vec4(0);
    float firstDepth = -1;

    // 从第一个采样区间的中点开始，由近到远遍历体数据。
    for (float t = enter + stepSize * 0.5; t < leave; t += stepSize)
    {
        vec3 position = origin + direction * t;
        float hu = sampleVolume(position);
        // 窗位和窗宽将 HU 值映射到 [0, 1]，用于颜色和透明度计算。
        float value = clamp(
            (hu - view.transfer.x) / view.transfer.y + 0.5, 0.0, 1.0);


        // 血管模式突出高密度血管，同时保留较淡的周围组织作为参照。
        // extinction 是单位长度的消光系数，单位为 1/mm。
        float vessel = smoothstep(0.18, 0.60, value);
        float context = smoothstep(-180.0, 40.0, hu);
        float extinction = value * value * 0.08;

        if (view.transfer.w > 0.5)
            extinction = mix(0.0008 * context, 0.65, vessel);

        // 将消光系数、透明度倍率和实际步长转换为本次采样的不透明度。
        float alpha = 1.0 - exp(-extinction * view.transfer.z * stepSize);

        if (alpha <= 0.0)
        {
            continue;
        }

        // 默认组织采用棕色到浅暖色的渐变；血管模式使用单独的配色。
        vec3 tissue = mix(
            vec3(0.55, 0.22, 0.12), vec3(1.0, 0.96, 0.86), value);

        if (view.transfer.w > 0.5)
        {
            vec3 vesselColor = mix(
                vec3(0.85, 0.12, 0.025),
                vec3(1.0, 0.82, 0.48),
                smoothstep(0.35, 0.85, value));
            tissue = mix(vec3(0.32, 0.38, 0.46), vesselColor, vessel);
        }

        // 先计算局部光照，再按剩余透光率进行从前向后的颜色合成。
        tissue = shadeVolume(tissue, position, -direction, toLight);
        accumulated += vec4((1 - accumulated.a) * alpha * tissue, 0.0);
        accumulated.a += (1 - accumulated.a) * alpha;

        // 累计不透明度首次超过阈值时，记录用于遮挡判断的深度。
        if (firstDepth < 0 && accumulated.a > 0.1)
            firstDepth = t;

        // 已接近完全不透明，后续采样贡献很小，可以提前结束。
        if (accumulated.a > 0.98)
            break;
    }

    // 几乎透明的光线不写入，保留背景。
    if (accumulated.a < 0.01)
        discard;

    // 将累计颜色与黑色背景合成，输出不透明像素。
    color = vec4(accumulated.rgb + (1 - accumulated.a) * vec3(0.0), 1);
    // 正交投影下，将采样距离归一化为深度，供后续绘制进行深度测试。
    gl_FragDepth = (firstDepth < 0 ? leave : firstDepth) / length(end - origin);
}

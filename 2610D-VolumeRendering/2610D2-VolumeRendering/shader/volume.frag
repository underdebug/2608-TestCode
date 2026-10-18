#version 450
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 color;
#ifdef __cplusplus
std::vector<uint> voxels;
#else
layout(set=0,binding=0,std430) readonly buffer Volume { uint voxels[]; };
#endif
layout(push_constant) uniform View
{
    mat4 inverseMatrix;
    vec4 halfSize;
    ivec4 dimensions;
    vec4 transfer;
} view;
float voxel(ivec3 p)
{
    p = clamp(p, ivec3(0), view.dimensions.xyz - 1);
    int index = (p.z * view.dimensions.y + p.y) * view.dimensions.x + p.x;
    uint raw = (voxels[index / 2] >> ((index & 1) * 16)) & 65535u;
    return float(int(raw << 16) >> 16);
}
float sampleVolume(vec3 position)
{
    vec3 p = (position / (2.0 * view.halfSize.xyz) + 0.5) * vec3(view.dimensions.xyz) - 0.5;
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p);
    return mix(mix(mix(voxel(i), voxel(i+ivec3(1,0,0)), f.x),
                   mix(voxel(i+ivec3(0,1,0)), voxel(i+ivec3(1,1,0)), f.x), f.y),
               mix(mix(voxel(i+ivec3(0,0,1)), voxel(i+ivec3(1,0,1)), f.x),
                   mix(voxel(i+ivec3(0,1,1)), voxel(i+ivec3(1,1,1)), f.x), f.y), f.z);
}
// Central differences in HU/mm keep normals correct for anisotropic voxels.
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

vec3 shadeVolume(vec3 tissue, vec3 position, vec3 toEye, vec3 toLight)
{
    vec3 gradient = volumeGradient(position);
    float magnitude = length(gradient);
    // Homogeneous regions have no meaningful surface normal.
    if (magnitude < 1e-4)
    {
        return tissue * 0.65;
    }

    // Increasing CT density points into tissue, so negate for an outward normal.
    vec3 normal = -gradient / magnitude;
    if (dot(normal, toEye) < 0.0)
    {
        normal = -normal;
    }
    float diffuse = max(dot(normal, toLight), 0.0);
    vec3 halfway = normalize(toLight + toEye);
    float specular = diffuse > 0.0 ? pow(max(dot(normal, halfway), 0.0), 40.0) : 0.0;
    // Fade highlights in almost uniform regions to avoid amplifying noise.
    specular *= smoothstep(1.0, 20.0, magnitude);
    return tissue * (0.55 + 0.65 * diffuse) + vec3(0.65 * specular);
}

void main()
{
    vec3 origin = (view.inverseMatrix * vec4(ndc,0,1)).xyz;
    vec3 end = (view.inverseMatrix * vec4(ndc,1,1)).xyz;
    vec3 direction = normalize(end-origin);
    // Camera-relative light: above and to the right of the viewer. All vectors
    // remain in volume space, including after camera rotation.
    vec3 cameraRight = normalize(view.inverseMatrix[0].xyz);
    vec3 cameraUp = -normalize(view.inverseMatrix[1].xyz);
    vec3 toLight = normalize(-direction + 0.45 * cameraRight + 0.65 * cameraUp);
    vec3 invDirection = 1.0 / mix(direction, vec3(1e-8), lessThan(abs(direction),vec3(1e-8)));
    vec3 a = (-view.halfSize.xyz-origin)*invDirection;
    vec3 b = (view.halfSize.xyz-origin)*invDirection;
    vec3 lo = min(a,b), hi = max(a,b);
    float enter = max(max(lo.x,lo.y),max(lo.z,0.0));
    float leave = min(min(hi.x,hi.y),hi.z);
    if (leave <= enter) discard;
    // Resolve thin structures with at least two samples per smallest voxel.
    // Keep a bounded workload for unusually large volumes.
    vec3 spacing = 2.0 * view.halfSize.xyz / vec3(view.dimensions.xyz);
    float stepSize = max((leave-enter)/2048.0, 0.5 * min(min(spacing.x,spacing.y),spacing.z));
    vec4 accumulated = vec4(0);
    float firstDepth = -1;
    for (float t=enter+stepSize*0.5; t<leave; t+=stepSize)
    {
        vec3 position = origin + direction * t;
        float hu = sampleVolume(position);
        float value = clamp((hu-view.transfer.x)/view.transfer.y+0.5,0.0,1.0);
        // The vessel preset separates faint tissue context from contrast-filled
        // blood. Extinction is in inverse mm, independent of sample spacing.
        float vessel = smoothstep(0.18, 0.60, value);
        float context = smoothstep(-180.0, 40.0, hu);
        float extinction = value * value * 0.08;
        if (view.transfer.w > 0.5)
            extinction = mix(0.0008 * context, 0.65, vessel);
        float alpha = 1.0 - exp(-extinction * view.transfer.z * stepSize);
        if (alpha <= 0.0)
        {
            continue;
        }
        vec3 tissue = mix(vec3(0.55,0.22,0.12),vec3(1.0,0.96,0.86),value);
        if (view.transfer.w > 0.5)
        {
            vec3 vesselColor = mix(vec3(0.85, 0.12, 0.025),
                                   vec3(1.0, 0.82, 0.48), smoothstep(0.35, 0.85, value));
            tissue = mix(vec3(0.32, 0.38, 0.46), vesselColor, vessel);
        }
        tissue = shadeVolume(tissue, position, -direction, toLight);
        accumulated += vec4((1-accumulated.a)*alpha*tissue, 0.0);
        accumulated.a += (1-accumulated.a)*alpha;
        if (firstDepth < 0 && accumulated.a > 0.1) firstDepth = t;
        if (accumulated.a > 0.98) break;
    }
    if (accumulated.a < 0.01) discard;
    color = vec4(accumulated.rgb+(1-accumulated.a)*vec3(0.0),1);
    gl_FragDepth = (firstDepth < 0 ? leave : firstDepth)/length(end-origin);
}

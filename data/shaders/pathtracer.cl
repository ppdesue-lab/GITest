struct BVHNode
{
    float4 lmin;
    float4 lmax;
    float4 rmin;
    float4 rmax;
};

struct TraceMaterial
{
    float4 diffuseRoughness;
    float4 specularMetallic;
    float4 textureInfo;
};

#define PI 3.14159265358979323846f
#define STACK_SIZE 48

uint NextRandom(uint* state)
{
    *state = *state * 747796405u + 2891336453u;
    uint word = ((*state >> ((*state >> 28u) + 4u)) ^ *state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(uint* state)
{
    return (float)NextRandom(state) * (1.0f / 4294967296.0f);
}

float3 SampleEnvironment(const global float4* environment, int width, int height, float3 direction)
{
    float u = atan2(direction.z, direction.x) * (0.5f / PI) + 0.5f;
    float v = acos(clamp(direction.y, -1.0f, 1.0f)) / PI;
    int x = clamp((int)(u * (float)width), 0, width - 1);
    int y = clamp((int)(v * (float)height), 0, height - 1);
    return environment[y * width + x].xyz;
}

float3 SampleDiffuseTexture(const global float4* texels, float4 textureInfo, float2 uv)
{
    if (textureInfo.w < 0.5f)
        return (float3)(1.0f);
    int offset = (int)textureInfo.x;
    int width = (int)textureInfo.y;
    int height = (int)textureInfo.z;
    uv = uv - floor(uv);
    int x = clamp((int)(uv.x * (float)width), 0, width - 1);
    int y = clamp((int)(uv.y * (float)height), 0, height - 1);
    float3 srgb = texels[offset + y * width + x].xyz;
    return pow(clamp(srgb, (float3)(0.0f), (float3)(1.0f)), (float3)(2.2f));
}

float3 CosineHemisphere(float3 normal, uint* state)
{
    float r1 = RandomFloat(state);
    float r2 = RandomFloat(state);
    float phi = 2.0f * PI * r1;
    float r = sqrt(r2);
    float3 tangent = normalize(fabs(normal.x) > 0.1f ? cross((float3)(0.0f, 1.0f, 0.0f), normal)
        : cross((float3)(1.0f, 0.0f, 0.0f), normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * (cos(phi) * r) + bitangent * (sin(phi) * r) + normal * sqrt(1.0f - r2));
}

bool IntersectScene(const global struct BVHNode* nodes, const global uint* indices, const global float4* triangles,
    uint triangleCount, float3 origin, float3 direction, float* hitDistance, uint* hitPrimitive, float2* hitUV)
{
    if (triangleCount == 0)
        return false;
    float closest = 1.0e30f;
    float3 inverseDirection = native_recip(direction);
    uint node = 0;
    uint stack[STACK_SIZE];
    uint stackPointer = 0;
    while (1)
    {
        const float4 lmin = nodes[node].lmin;
        const float4 lmax = nodes[node].lmax;
        const float4 rmin = nodes[node].rmin;
        const float4 rmax = nodes[node].rmax;
        const uint leafCount = as_uint(rmin.w);
        if (leafCount > 0)
        {
            const uint first = as_uint(rmax.w);
            for (uint i = 0; i < leafCount; ++i)
            {
                const uint primitive = indices[first + i];
                const global float4* triangle = triangles + primitive * 3;
                const float3 edge1 = triangle[1].xyz - triangle[0].xyz;
                const float3 edge2 = triangle[2].xyz - triangle[0].xyz;
                const float3 p = cross(direction, edge2);
                const float determinant = dot(edge1, p);
                if (fabs(determinant) < 1.0e-8f)
                    continue;
                const float inverseDeterminant = native_recip(determinant);
                const float3 t = origin - triangle[0].xyz;
                const float u = dot(t, p) * inverseDeterminant;
                if (u < 0.0f || u > 1.0f)
                    continue;
                const float3 q = cross(t, edge1);
                const float v = dot(direction, q) * inverseDeterminant;
                if (v < 0.0f || u + v > 1.0f)
                    continue;
                const float distance = dot(edge2, q) * inverseDeterminant;
                if (distance > 0.0005f && distance < closest)
                {
                    closest = distance;
                    *hitPrimitive = primitive;
                    *hitUV = (float2)(u, v);
                }
            }
            if (stackPointer == 0)
                break;
            node = stack[--stackPointer];
            continue;
        }

        uint left = as_uint(lmin.w);
        uint right = as_uint(lmax.w);
        float3 ta0 = (lmin.xyz - origin) * inverseDirection;
        float3 ta1 = (lmax.xyz - origin) * inverseDirection;
        float3 tb0 = (rmin.xyz - origin) * inverseDirection;
        float3 tb1 = (rmax.xyz - origin) * inverseDirection;
        float amin = fmax(fmax(fmax(fmin(ta0.x, ta1.x), fmin(ta0.y, ta1.y)), fmin(ta0.z, ta1.z)), 0.0f);
        float amax = fmin(fmin(fmin(fmax(ta0.x, ta1.x), fmax(ta0.y, ta1.y)), fmax(ta0.z, ta1.z)), closest);
        float bmin = fmax(fmax(fmax(fmin(tb0.x, tb1.x), fmin(tb0.y, tb1.y)), fmin(tb0.z, tb1.z)), 0.0f);
        float bmax = fmin(fmin(fmin(fmax(tb0.x, tb1.x), fmax(tb0.y, tb1.y)), fmax(tb0.z, tb1.z)), closest);
        float leftDistance = amin > amax ? 1.0e30f : amin;
        float rightDistance = bmin > bmax ? 1.0e30f : bmin;
        if (leftDistance > rightDistance)
        {
            float distanceSwap = leftDistance;
            leftDistance = rightDistance;
            rightDistance = distanceSwap;
            uint nodeSwap = left;
            left = right;
            right = nodeSwap;
        }
        if (leftDistance == 1.0e30f)
        {
            if (stackPointer == 0)
                break;
            node = stack[--stackPointer];
        }
        else
        {
            node = left;
            if (rightDistance != 1.0e30f && stackPointer < STACK_SIZE)
                stack[stackPointer++] = right;
        }
    }
    if (closest == 1.0e30f)
        return false;
    *hitDistance = closest;
    return true;
}

float3 TracePath(const global struct BVHNode* nodes, const global uint* indices, const global float4* triangles,
    const global float4* triangleUVs, const global struct TraceMaterial* materials, const global float4* diffuseTexels,
    const global float4* environment, int envWidth, int envHeight,
    uint triangleCount, float3 origin, float3 direction, uint* state)
{
    float3 radiance = (float3)(0.0f);
    float3 throughput = (float3)(1.0f);
    for (uint bounce = 0; bounce < 4; ++bounce)
    {
        float distance;
        uint primitive;
        float2 barycentric;
        if (!IntersectScene(nodes, indices, triangles, triangleCount, origin, direction, &distance, &primitive, &barycentric))
        {
            radiance += throughput * SampleEnvironment(environment, envWidth, envHeight, direction);
            break;
        }

        const global float4* triangle = triangles + primitive * 3;
        float3 normal = normalize(cross(triangle[1].xyz - triangle[0].xyz, triangle[2].xyz - triangle[0].xyz));
        if (dot(normal, direction) > 0.0f)
            normal = -normal;
        const struct TraceMaterial material = materials[primitive];
        const global float4* uvs = triangleUVs + primitive * 3;
        const float2 uv = uvs[0].xy * (1.0f - barycentric.x - barycentric.y) +
            uvs[1].xy * barycentric.x + uvs[2].xy * barycentric.y;
        const float3 diffuse = material.diffuseRoughness.xyz *
            SampleDiffuseTexture(diffuseTexels, material.textureInfo, uv);
        const float roughness = material.diffuseRoughness.w;
        const float3 specular = material.specularMetallic.xyz;
        const float fresnelFactor = pow(1.0f - fmax(dot(-direction, normal), 0.0f), 5.0f);
        const float3 fresnel = specular + ((float3)(1.0f) - specular) * fresnelFactor;
        const float specularProbability = clamp(fmax(fmax(fresnel.x, fresnel.y), fresnel.z), 0.08f, 0.92f);

        origin += direction * distance + normal * 0.001f;
        if (RandomFloat(state) < specularProbability)
        {
            float3 reflected = direction - 2.0f * dot(direction, normal) * normal;
            float3 roughDirection = CosineHemisphere(normalize(reflected + normal * 0.001f), state);
            direction = normalize(mix(reflected, roughDirection, roughness * roughness));
            throughput *= fresnel / specularProbability;
        }
        else
        {
            direction = CosineHemisphere(normal, state);
            throughput *= diffuse * ((float3)(1.0f) - fresnel) / (1.0f - specularProbability);
        }
        throughput = min(throughput, (float3)(8.0f));
    }
    return radiance;
}

void ComputePrimaryFeatures(const global struct BVHNode* nodes, const global uint* indices,
    const global float4* triangles, const global float4* triangleUVs,
    const global struct TraceMaterial* materials, const global float4* diffuseTexels,
    uint triangleCount, float3 origin, float3 direction,
    global float4* positions, global float4* normals, global float4* albedos, uint index)
{
    float distance;
    uint primitive;
    float2 barycentric;
    if (!IntersectScene(nodes, indices, triangles, triangleCount, origin, direction, &distance, &primitive, &barycentric))
    {
        positions[index] = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
        normals[index] = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
        albedos[index] = (float4)(1.0f);
        return;
    }

    const global float4* triangle = triangles + primitive * 3;
    float3 normal = normalize(cross(triangle[1].xyz - triangle[0].xyz, triangle[2].xyz - triangle[0].xyz));
    if (dot(normal, direction) > 0.0f)
        normal = -normal;

    const struct TraceMaterial material = materials[primitive];
    const global float4* uvs = triangleUVs + primitive * 3;
    const float2 uv = uvs[0].xy * (1.0f - barycentric.x - barycentric.y) +
        uvs[1].xy * barycentric.x + uvs[2].xy * barycentric.y;
    const float3 diffuse = material.diffuseRoughness.xyz *
        SampleDiffuseTexture(diffuseTexels, material.textureInfo, uv);

    positions[index] = (float4)(origin + direction * distance, 1.0f);
    normals[index] = (float4)(normal, 1.0f);
    albedos[index] = (float4)(clamp(diffuse, (float3)(0.0f), (float3)(1.0f)), 1.0f);
}

kernel void PathTrace(const global struct BVHNode* nodes, const global uint* indices, const global float4* triangles,
    const global float4* triangleUVs, const global struct TraceMaterial* materials, const global float4* diffuseTexels,
    const global float4* environment, int envWidth, int envHeight,
    uint triangleCount, float4 eye, float4 bottomLeft, float4 bottomRight, float4 topLeft,
    uint width, uint height, uint sampleIndex, global float4* accumulation, global float4* pixels,
    global float4* rawRadiance, global float4* positions, global float4* normals, global float4* albedos)
{
    uint index = get_global_id(0);
    if (index >= width * height)
        return;
    uint x = index % width;
    uint y = index / width;
    uint state = index * 9781u + (sampleIndex + 1u) * 6271u + 89173u;
    float u = ((float)x + RandomFloat(&state)) / (float)width;
    float v = ((float)y + RandomFloat(&state)) / (float)height;
    float3 direction = normalize(bottomLeft.xyz + (bottomRight.xyz - bottomLeft.xyz) * u +
        (topLeft.xyz - bottomLeft.xyz) * v);
    ComputePrimaryFeatures(nodes, indices, triangles, triangleUVs, materials, diffuseTexels,
        triangleCount, eye.xyz, direction, positions, normals, albedos, index);
    float3 sample = TracePath(nodes, indices, triangles, triangleUVs, materials, diffuseTexels, environment, envWidth, envHeight,
        triangleCount, eye.xyz, direction, &state);
    rawRadiance[index] = (float4)(fmax(sample, (float3)(0.0f)), 1.0f);
    float3 sum = accumulation[index].xyz + sample;
    accumulation[index] = (float4)(sum, 1.0f);
    float3 color = sum / (float)(sampleIndex + 1u);
    color = color / ((float3)(1.0f) + color);
    color = pow(clamp(color, (float3)(0.0f), (float3)(1.0f)), (float3)(1.0f / 2.2f));
    pixels[index] = (float4)(color, 1.0f);
}

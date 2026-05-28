#define EPSILON 0.00001f

float Luminance(float3 color)
{
    return dot(color, (float3)(0.2126f, 0.7152f, 0.0722f));
}

float3 Tonemap(float3 color)
{
    color = color / ((float3)(1.0f) + color);
    return pow(clamp(color, (float3)(0.0f), (float3)(1.0f)), (float3)(1.0f / 2.2f));
}

float4 MulMat4Point(float16 m, float3 p)
{
    float4 v = (float4)(p.x, p.y, p.z, 1.0f);
    return (float4)(
        m.s0 * v.x + m.s4 * v.y + m.s8 * v.z + m.sc * v.w,
        m.s1 * v.x + m.s5 * v.y + m.s9 * v.z + m.sd * v.w,
        m.s2 * v.x + m.s6 * v.y + m.sa * v.z + m.se * v.w,
        m.s3 * v.x + m.s7 * v.y + m.sb * v.z + m.sf * v.w);
}

kernel void TemporalAccumulation(
    const global float4* currentColor,
    const global float4* currentPosition,
    const global float4* currentNormal,
    const global float4* currentAlbedo,
    const global float4* historyColor,
    const global float4* historyMoments,
    const global float4* historyPosition,
    const global float4* historyNormal,
    const global float4* historyAlbedo,
    uint width,
    uint height,
    float historyAlpha,
    float16 previousViewProjection,
    int hasHistory,
    global float4* temporalColor,
    global float4* temporalMoments)
{
    uint index = get_global_id(0);
    if (index >= width * height)
        return;

    float4 color = currentColor[index];
    float4 position = currentPosition[index];
    float4 normal = currentNormal[index];
    float4 albedo = currentAlbedo[index];
    float lum = Luminance(color.xyz);
    float4 moments = (float4)(lum, lum * lum, 1.0f, 0.0f);

    if (hasHistory && position.w > 0.5f)
    {
        float4 clip = MulMat4Point(previousViewProjection, position.xyz);
        if (fabs(clip.w) > EPSILON)
        {
            float2 uv = clip.xy / clip.w * 0.5f + 0.5f;
            int px = (int)floor(uv.x * (float)width + 0.5f);
            int py = (int)floor(uv.y * (float)height + 0.5f);
            if (px >= 0 && py >= 0 && px < (int)width && py < (int)height)
            {
                uint prevIndex = (uint)py * width + (uint)px;
                float4 prevPosition = historyPosition[prevIndex];
                float4 prevNormal = historyNormal[prevIndex];
                float4 prevAlbedo = historyAlbedo[prevIndex];
                float positionScale = fmax(length(position.xyz), 1.0f);
                float positionDelta = length(prevPosition.xyz - position.xyz) / positionScale;
                float normalMatch = dot(normalize(prevNormal.xyz), normalize(normal.xyz));
                float albedoDelta = length(prevAlbedo.xyz - albedo.xyz);

                if (prevPosition.w > 0.5f && positionDelta < 0.025f && normalMatch > 0.85f && albedoDelta < 0.35f)
                {
                    float clampedAlpha = clamp(historyAlpha, 0.0f, 0.98f);
                    float4 previousColor = historyColor[prevIndex];
                    float4 previousMoments = historyMoments[prevIndex];
                    color = mix(color, previousColor, clampedAlpha);
                    moments.xy = mix(moments.xy, previousMoments.xy, clampedAlpha);
                    moments.z = fmin(previousMoments.z + 1.0f, 32.0f);
                }
            }
        }
    }

    temporalColor[index] = (float4)(fmax(color.xyz, (float3)(0.0f)), 1.0f);
    temporalMoments[index] = moments;
}

kernel void AtrousFilter(
    const global float4* inputColor,
    const global float4* position,
    const global float4* normal,
    const global float4* albedo,
    const global float4* moments,
    uint width,
    uint height,
    int stepSize,
    float phiColor,
    float phiNormal,
    float phiDepth,
    global float4* outputColor)
{
    uint index = get_global_id(0);
    if (index >= width * height)
        return;

    uint x = index % width;
    uint y = index / width;
    float4 centerPosition = position[index];
    if (centerPosition.w < 0.5f)
    {
        outputColor[index] = inputColor[index];
        return;
    }

    float3 centerColor = inputColor[index].xyz;
    float3 centerNormal = normalize(normal[index].xyz);
    float3 centerAlbedo = albedo[index].xyz;
    float variance = fmax(moments[index].y - moments[index].x * moments[index].x, 0.0f);
    float colorSigma = fmax(phiColor * sqrt(variance + EPSILON), 0.02f);
    float depthSigma = fmax(phiDepth, 0.001f);

    const float filterWeights[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };
    float3 sum = (float3)(0.0f);
    float weightSum = 0.0f;

    for (int oy = -2; oy <= 2; ++oy)
    {
        int sy = (int)y + oy * stepSize;
        if (sy < 0 || sy >= (int)height)
            continue;
        for (int ox = -2; ox <= 2; ++ox)
        {
            int sx = (int)x + ox * stepSize;
            if (sx < 0 || sx >= (int)width)
                continue;

            uint sampleIndex = (uint)sy * width + (uint)sx;
            float4 samplePosition = position[sampleIndex];
            if (samplePosition.w < 0.5f)
                continue;

            float3 sampleColor = inputColor[sampleIndex].xyz;
            float3 sampleNormal = normalize(normal[sampleIndex].xyz);
            float3 sampleAlbedo = albedo[sampleIndex].xyz;
            float normalWeight = pow(fmax(dot(centerNormal, sampleNormal), 0.0f), phiNormal);
            float depthWeight = exp(-length(samplePosition.xyz - centerPosition.xyz) / depthSigma);
            float colorWeight = exp(-length(sampleColor - centerColor) / colorSigma);
            float albedoWeight = exp(-length(sampleAlbedo - centerAlbedo) * 8.0f);
            float kernelWeight = filterWeights[ox + 2] * filterWeights[oy + 2];
            float weight = kernelWeight * normalWeight * depthWeight * colorWeight * albedoWeight;
            sum += sampleColor * weight;
            weightSum += weight;
        }
    }

    float3 filtered = weightSum > EPSILON ? sum / weightSum : centerColor;
    outputColor[index] = (float4)(fmax(filtered, (float3)(0.0f)), 1.0f);
}

kernel void FinalizeSVGF(
    const global float4* filteredColor,
    const global float4* position,
    const global float4* normal,
    const global float4* albedo,
    const global float4* moments,
    uint width,
    uint height,
    global float4* historyColor,
    global float4* historyMoments,
    global float4* historyPosition,
    global float4* historyNormal,
    global float4* historyAlbedo,
    global float4* pixels)
{
    uint index = get_global_id(0);
    if (index >= width * height)
        return;

    float4 color = filteredColor[index];
    historyColor[index] = color;
    historyMoments[index] = moments[index];
    historyPosition[index] = position[index];
    historyNormal[index] = normal[index];
    historyAlbedo[index] = albedo[index];
    pixels[index] = (float4)(Tonemap(color.xyz), 1.0f);
}
